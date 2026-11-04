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
from __future__ import annotations

import tempfile
import types
from pathlib import Path
from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import patch

from webkitbuilddiff import artifacts

CODESIGN_DISPLAY = """\
Executable=/b/JavaScriptCore.framework/Versions/A/JavaScriptCore
Identifier=JavaScriptCore
Format=bundle with Mach-O thin (arm64e)
CodeDirectory v=20400 size=297137 flags=0x20002(adhoc,linker-signed) hashes=9279+3 location=embedded
Signature=adhoc
Info.plist=not bound
TeamIdentifier=not set
Sealed Resources=none
"""

ENTITLEMENTS_XML = b"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>com.apple.security.cs.allow-jit</key><true/>
</dict></plist>
"""


class CodesignInfoTest(TestCase):
    def _fake_run(self, entitlements=ENTITLEMENTS_XML, display=CODESIGN_DISPLAY,
                  display_rc=0):
        def run(cmd, **kwargs):
            if '--entitlements' in cmd:
                return SimpleNamespace(returncode=0, stdout=entitlements,
                                       stderr=b'')
            return SimpleNamespace(returncode=display_rc, stdout='',
                                   stderr=display)
        return run

    def test_parses_adhoc_signature(self):
        with patch('webkitbuilddiff.artifacts.subprocess.run',
                   side_effect=self._fake_run()):
            info = artifacts.codesign_info(Path('/b/x'))
        self.assertTrue(info.signed)
        self.assertEqual(info.identifier, 'JavaScriptCore')
        self.assertEqual(info.authority, [])
        self.assertEqual(info.flags, '0x20002(adhoc,linker-signed)')
        self.assertIsNone(info.team_id)
        self.assertIn('com.apple.security.cs.allow-jit', info.entitlements)

    def test_authority_and_team_populated(self):
        display = ('Identifier=com.apple.JavaScriptCore\n'
                   'CodeDirectory v=20400 flags=0x0(none) location=embedded\n'
                   'Authority=Safari Engineering\n'
                   'Authority=Apple Root CA\n'
                   'TeamIdentifier=ABCDE12345\n')
        with patch('webkitbuilddiff.artifacts.subprocess.run',
                   side_effect=self._fake_run(display=display, entitlements=b'')):
            info = artifacts.codesign_info(Path('/b/x'))
        self.assertEqual(info.authority, ['Safari Engineering', 'Apple Root CA'])
        self.assertEqual(info.team_id, 'ABCDE12345')
        self.assertEqual(info.entitlements, {})

    def test_unsigned_binary(self):
        def run(cmd, **kwargs):
            return SimpleNamespace(
                returncode=1, stdout='',
                stderr='/b/x: code object is not signed at all\n')
        with patch('webkitbuilddiff.artifacts.subprocess.run', side_effect=run):
            info = artifacts.codesign_info(Path('/b/x'))
        self.assertFalse(info.signed)


class ResolveBinaryTest(TestCase):
    def test_direct_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'jsc'
            binary.write_bytes(b'\xcf\xfa\xed\xfe')
            product = _product(binary)
            self.assertEqual(artifacts.resolve_binary(product), binary)

    def test_framework_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            fw = Path(tmp) / 'Foo.framework'
            (fw / 'Versions' / 'A').mkdir(parents=True)
            binary = fw / 'Versions' / 'A' / 'Foo'
            binary.write_bytes(b'\xcf\xfa\xed\xfe')
            self.assertEqual(artifacts.resolve_binary(_product(fw)), binary)

    def test_missing(self):
        self.assertIsNone(artifacts.resolve_binary(_product(Path('/nope/x'))))


class ExportedSymbolsTest(TestCase):
    def test_delegates_to_apireport(self):
        fake_report = SimpleNamespace(exports={'_a', '_b'})
        fake_macho = types.ModuleType('webkitapipy.macho')
        fake_macho.APIReport = SimpleNamespace(  # type: ignore[attr-defined]
            from_binary=lambda binary, arch, exports_only: fake_report)
        modules = {
            'webkitapipy': types.ModuleType('webkitapipy'),
            'webkitapipy.macho': fake_macho,
        }
        with patch.dict('sys.modules', modules):
            symbols = artifacts.exported_symbols(Path('/b/x'), arch='arm64e')
        self.assertEqual(symbols, {'_a', '_b'})


def _product(path: Path):
    from webkitbuilddiff.model import Product, ProductKind
    return Product(identity=path.name, kind=ProductKind.EXECUTABLE,
                   output_path=path, target_name='t')
