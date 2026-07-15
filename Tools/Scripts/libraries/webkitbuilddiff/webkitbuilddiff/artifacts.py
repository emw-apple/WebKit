# Copyright (C) 2026 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Inspection of produced binaries: exported symbols and codesigning.

Unlike compiler/linker flags (which come from the build graph), a binary's
exported symbols and its signature are ground truth read from the artifact
itself, and are obtained identically for both build systems. Symbol extraction
reuses :class:`webkitapipy.macho.APIReport`; signing state comes from
``codesign -d``.
"""
from __future__ import annotations

import plistlib
import re
import subprocess
from pathlib import Path
from typing import Optional

from webkitbuilddiff.model import CodesignInfo, Product, ProductKind

# Kinds that are Mach-O images we can introspect (static archives are not).
INSPECTABLE_KINDS = frozenset({
    ProductKind.FRAMEWORK, ProductKind.DYLIB, ProductKind.EXECUTABLE,
    ProductKind.APP_BUNDLE, ProductKind.BUNDLE,
})

_CODESIGN_FIELD = re.compile(r'^(?P<key>[A-Za-z ]+)=(?P<value>.*)$')
_FLAGS = re.compile(r'\bflags=(\S+)')


def resolve_binary(product: Product) -> Optional[Path]:
    """Return the Mach-O file for a product, or None if it is not on disk.

    Both extractors record the linker's output, which is the Mach-O image
    itself (e.g. ``JavaScriptCore.framework/Versions/A/JavaScriptCore``), so the
    product's ``output_path`` is normally the file we want.
    """
    path = product.output_path
    if path.is_file():
        return path
    # Fall back to the primary binary inside a bundle directory, if handed one.
    if path.is_dir():
        stem = path.name.split('.', 1)[0]
        for candidate in (path / stem,
                          path / 'Versions' / 'A' / stem,
                          path / 'Contents' / 'MacOS' / stem):
            if candidate.is_file():
                return candidate
    return None


def exported_symbols(binary: Path, *, arch: str) -> set[str]:
    """Return the set of exported symbol names from a Mach-O image."""
    # Imported lazily so the rest of the tool works without webkitapipy present.
    from webkitapipy.macho import APIReport  # type: ignore[import-not-found]
    report = APIReport.from_binary(binary, arch=arch, exports_only=True)
    return set(report.exports)


def codesign_info(binary: Path) -> CodesignInfo:
    """Read ground-truth signing state from a binary via ``codesign -d``."""
    display = subprocess.run(
        ('codesign', '-d', '-vv', str(binary)),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    # codesign prints its report to stderr.
    output = display.stderr or display.stdout
    if display.returncode != 0 and 'not signed' in output:
        return CodesignInfo(signed=False)

    info = CodesignInfo()
    for line in output.splitlines():
        # The CodeDirectory line is "CodeDirectory v=.. flags=0x2(adhoc) .."
        # rather than a plain key=value pair.
        if line.startswith('CodeDirectory'):
            flags = _FLAGS.search(line)
            if flags:
                info.flags = flags.group(1)
            continue
        match = _CODESIGN_FIELD.match(line)
        if not match:
            continue
        key = match.group('key')
        value = match.group('value')
        if key == 'Identifier':
            info.identifier = value
        elif key == 'TeamIdentifier':
            info.team_id = None if value == 'not set' else value
        elif key == 'Format':
            info.format = value
        elif key == 'Authority':
            info.authority.append(value)

    info.entitlements = _entitlements(binary)
    return info


def _entitlements(binary: Path) -> dict[str, object]:
    """Return a binary's embedded entitlements as a plist dict (empty if none)."""
    result = subprocess.run(
        ('codesign', '-d', '--entitlements', ':-', '--xml', str(binary)),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    data = result.stdout.strip()
    if not data:
        return {}
    try:
        parsed = plistlib.loads(data)
    except (plistlib.InvalidFileException, ValueError):
        return {}
    return parsed if isinstance(parsed, dict) else {}
