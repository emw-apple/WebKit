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

import json
import tempfile
from pathlib import Path
from unittest import TestCase

from webkitbuilddiff import xcode
from webkitbuilddiff.model import ProductKind

GUID = 'a' * 64


class XcodeLoadTest(TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        self.resp = self.dir / '1234-common-args.resp'
        self.resp.write_text("'-std=c++2b' -O0 -DFOO=1 -iquote /src/hmap")
        commands = {
            f'P0:target-Foo-{GUID}-:Debug:CompileC /b/foo.o': {
                'tool': 'ccompile',
                'description': 'CompileC /b/foo.o /src/foo.cpp normal arm64e c++',
                'inputs': [str(self.resp), '/src/foo.cpp', '/b/foo.o.scan',
                           f'<target-Foo-{GUID}--begin-compiling>'],
                'outputs': ['/b/foo.o'],
            },
            f'P2:target-Foo-{GUID}-:Debug:Ld /b/Foo.framework/Versions/A/Foo': {
                'tool': 'shell',
                'description': 'Ld /b/Foo.framework/Versions/A/Foo normal',
                'inputs': ['/b/foo.o'],
                'outputs': ['/b/Foo.framework/Versions/A/Foo',
                            '<Linked Binary /b/Foo.framework/Versions/A/Foo>',
                            '/b/lto.o'],
                'args': ['/tc/clang++', '-dynamiclib', '-framework', 'Foundation',
                         '-o', '/b/Foo.framework/Versions/A/Foo'],
            },
            f'P0:target-Foo-{GUID}-:Debug:ProcessProductPackaging /src/Foo.entitlements /b/Foo.xcent': {
                'tool': 'process-product-entitlements',
                'description': 'ProcessProductPackaging /src/Foo.entitlements /b/Foo.xcent',
                'inputs': ['/src/Foo.entitlements', '/b/Entitlements.plist',
                           f'<target-Foo-{GUID}--immediate>'],
                'outputs': ['/b/Foo.xcent'],
            },
            # A static-archive libtool command.
            f'P1:target-Bar-{"b" * 64}-:Debug:Libtool /b/libBar.a normal': {
                'tool': 'linker',
                'description': 'Libtool /b/libBar.a normal',
                'inputs': ['/b/bar.o'],
                'outputs': ['/b/libBar.a'],
            },
        }
        self.manifest = self.dir / 'manifest.json'
        self.manifest.write_text(json.dumps(
            {'client': {}, 'targets': {}, 'nodes': {}, 'commands': commands}))

    def tearDown(self):
        self._tmp.cleanup()

    def test_compile_args_from_response_file(self):
        graph = xcode.load(self.manifest, build_root=Path('/b'))
        foo = graph.targets['Foo']
        self.assertEqual(len(foo.compile_steps), 1)
        self.assertEqual(
            foo.compile_steps[0].args,
            ['-std=c++2b', '-O0', '-DFOO=1', '-iquote', '/src/hmap'])
        self.assertEqual(foo.compile_steps[0].source, Path('/src/foo.cpp'))

    def test_link_product_and_args(self):
        graph = xcode.load(self.manifest, build_root=Path('/b'))
        products = graph.products()
        self.assertEqual(products['Foo.framework'].kind, ProductKind.FRAMEWORK)
        link = graph.targets['Foo'].link_step
        assert link is not None
        self.assertIn('-dynamiclib', link.args)
        self.assertEqual(link.linker, 'clang++')

    def test_static_archive(self):
        graph = xcode.load(self.manifest, build_root=Path('/b'))
        self.assertEqual(graph.products()['libBar.a'].kind,
                         ProductKind.STATIC_LIB)
        link = graph.targets['Bar'].link_step
        assert link is not None
        self.assertEqual(link.linker, 'libtool')

    def test_entitlements_annotation(self):
        graph = xcode.load(self.manifest, build_root=Path('/b'))
        self.assertEqual(graph.targets['Foo'].codesign_entitlements_source,
                         Path('/src/Foo.entitlements'))


class FindManifestTest(TestCase):
    def test_newest_manifest_selected(self):
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp)
            for name in ('old', 'new'):
                d = parent / f'{name}.xcbuilddata'
                d.mkdir()
                (d / 'manifest.json').write_text('{}')
            older = parent / 'old.xcbuilddata' / 'manifest.json'
            newer = parent / 'new.xcbuilddata' / 'manifest.json'
            import os
            os.utime(older, (1, 1))
            os.utime(newer, (2, 2))
            self.assertEqual(xcode.find_manifest(parent), newer)
