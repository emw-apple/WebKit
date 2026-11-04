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
"""Extract a :class:`~webkitbuilddiff.model.BuildGraph` from Xcode's llbuild manifest.

Xcode's build system (llbuild) persists the full build graph to
``<hash>.xcbuilddata/manifest.json``. The top-level ``commands`` dictionary is
keyed by ``P{phase}:target-{NAME}-{GUID}-:{Config}:{Task} {output}`` and each
value has a ``tool`` discriminator:

* ``ccompile``  - a compile. Flags live in a ``*-common-args.resp`` file listed
  in ``inputs`` (not inline); the source and object are the other inputs/outputs.
* ``shell`` with a ``Ld `` description - a link. The full clang++ invocation is
  in an inline ``args`` array.
* ``linker`` - ``libtool`` producing a static archive.
* ``process-product-entitlements`` - records the source ``.entitlements`` file
  that will be baked into the signed product.
"""
from __future__ import annotations

import json
import re
import shlex
from pathlib import Path
from typing import Optional

from webkitbuilddiff.model import (
    BuildGraph, BuildSystem, CompileStep, LinkStep, Product, Target,
    classify_product,
)

# P{phase}:target-{NAME}-{GUID}-:{Config}:{Task} {output...}
_COMMAND_KEY = re.compile(
    r'^P\d+:target-(?P<name>.+)-(?P<guid>[0-9a-f]{32,})-:'
    r'(?P<config>[^:]*):(?P<task>\S+)')

_SOURCE_SUFFIXES = ('.cpp', '.cc', '.cxx', '.c', '.mm', '.m', '.metal', '.S',
                    '.swift')


def find_manifest(xcbuilddata_parent: Path) -> Optional[Path]:
    """Return the newest ``manifest.json`` under ``XCBuildData/*.xcbuilddata``.

    Xcode keeps one ``.xcbuilddata`` directory per build description; the most
    recently written manifest corresponds to the current build.
    """
    candidates = sorted(
        xcbuilddata_parent.glob('*.xcbuilddata/manifest.json'),
        key=lambda p: p.stat().st_mtime, reverse=True)
    return candidates[0] if candidates else None


def _read_response_file(path: str, cache: dict[str, list[str]]) -> list[str]:
    if path in cache:
        return cache[path]
    try:
        tokens = shlex.split(Path(path).read_text())
    except OSError:
        tokens = []
    cache[path] = tokens
    return tokens


def _first_source(inputs: list[str]) -> Optional[str]:
    for value in inputs:
        if value.startswith('<'):
            continue
        if value.endswith(_SOURCE_SUFFIXES):
            return value
    return None


def _real_output(outputs: list[str]) -> Optional[str]:
    """Pick the produced binary from an Ld/Libtool command's outputs.

    Outputs also contain llbuild marker tokens (``<Linked Binary ...>``,
    ``<TRIGGER ...>``) and byproducts (``lto.o``, ``dependency_info.dat``).
    """
    for value in outputs:
        if value.startswith('<'):
            continue
        if value.endswith(('.o', '.dat')):
            continue
        return value
    return None


def load(manifest_path: Path, *, build_root: Path) -> BuildGraph:
    """Parse ``manifest_path`` into a BuildGraph rooted at ``build_root``.

    ``build_root`` is where the produced binaries live (e.g.
    ``WebKitBuild/Debug``); it is recorded so later phases can inspect them.
    """
    with open(manifest_path) as manifest_file:
        data = json.load(manifest_file)
    commands: dict[str, dict] = data.get('commands', {})

    graph = BuildGraph(build_system=BuildSystem.XCODE, root=build_root)
    response_cache: dict[str, list[str]] = {}

    def target_for(name: Optional[str]) -> Target:
        key = name or '<unknown>'
        target = graph.targets.get(key)
        if target is None:
            target = Target(name=key)
            graph.targets[key] = target
        return target

    for key, command in commands.items():
        tool = command.get('tool')
        description = command.get('description', '')
        match = _COMMAND_KEY.match(key)
        name = match.group('name') if match else None
        inputs = command.get('inputs', [])
        outputs = command.get('outputs', [])

        if tool == 'ccompile' and description.startswith('CompileC '):
            args: list[str] = []
            for value in inputs:
                if value.endswith('.resp'):
                    args += _read_response_file(value, response_cache)
            source = _first_source(inputs)
            output = outputs[0] if outputs else None
            target_for(name).compile_steps.append(CompileStep(
                source=Path(source) if source else None,
                output=Path(output) if output else None,
                args=args))

        elif tool == 'shell' and description.startswith('Ld '):
            output = _real_output(outputs)
            if not output:
                continue
            identity, kind = classify_product(Path(output))
            target = target_for(name)
            target.products.append(Product(
                identity=identity, kind=kind, output_path=Path(output),
                target_name=target.name))
            target.link_step = LinkStep(
                product_identity=identity, linker='clang++',
                args=list(command.get('args', [])))

        elif tool == 'linker':  # libtool static archive
            output = _real_output(outputs)
            if not output:
                continue
            identity, kind = classify_product(Path(output))
            target = target_for(name)
            target.products.append(Product(
                identity=identity, kind=kind, output_path=Path(output),
                target_name=target.name))
            target.link_step = LinkStep(
                product_identity=identity, linker='libtool',
                args=list(command.get('args', [])))

        elif tool == 'process-product-entitlements':
            source = inputs[0] if inputs else None
            if source:
                target_for(name).codesign_entitlements_source = Path(source)

    return graph
