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

from webkitbuilddiff import ninja
from webkitbuilddiff.model import ProductKind

BUILD_NINJA = """\
rule CXX_SHARED_LIBRARY_LINKER__JavaScriptCore_Debug
  command = clang++ $LINK_FLAGS -o $TARGET_FILE $in

build JavaScriptCore.framework/Versions/A/JavaScriptCore: \
CXX_SHARED_LIBRARY_LINKER__JavaScriptCore_Debug a.o b.o
  ARCH_FLAGS = -arch arm64e -isysroot /SDK
  LINK_FLAGS = -dynamiclib -Wl,-dead_strip
  LINK_LIBRARIES = -framework Foundation
  SONAME_FLAG = -install_name
  INSTALLNAME_DIR = @rpath/
  SONAME = JavaScriptCore.framework/Versions/A/JavaScriptCore
  TARGET_FILE = JavaScriptCore.framework/Versions/A/JavaScriptCore
  POST_BUILD = cd /x && codesign --force --sign - --entitlements JavaScriptCore.xcent JavaScriptCore.framework

build libANGLE.a: CXX_STATIC_LIBRARY_LINKER__ANGLE_Debug c.o
  TARGET_FILE = libANGLE.a
"""


class NinjaLoadTest(TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        (self.dir / 'build.ninja').write_text(BUILD_NINJA)
        compile_db = [{
            'directory': str(self.dir),
            'command': '/tc/clang++ -DFOO -O0 -stdlib=libc++ '
                       '-o Source/JavaScriptCore/CMakeFiles/JavaScriptCore.dir/x.cpp.o '
                       '-c /src/x.cpp',
            'file': '/src/x.cpp',
            'output': 'Source/JavaScriptCore/CMakeFiles/JavaScriptCore.dir/x.cpp.o',
        }]
        (self.dir / 'compile_commands.json').write_text(json.dumps(compile_db))

    def tearDown(self):
        self._tmp.cleanup()

    def test_compile_step_mapped_to_target(self):
        graph = ninja.load(self.dir)
        jsc = graph.targets['JavaScriptCore']
        self.assertEqual(len(jsc.compile_steps), 1)
        self.assertIn('-DFOO', jsc.compile_steps[0].args)
        self.assertEqual(jsc.compile_steps[0].source, Path('/src/x.cpp'))

    def test_link_product_and_assembled_args(self):
        graph = ninja.load(self.dir)
        product = graph.products()['JavaScriptCore.framework']
        self.assertEqual(product.kind, ProductKind.FRAMEWORK)
        link = graph.targets['JavaScriptCore'].link_step
        assert link is not None
        self.assertEqual(link.linker, 'clang++')
        for expected in ('-dynamiclib', '-framework', 'Foundation',
                         '-install_name', '@rpath/JavaScriptCore.framework/Versions/A/JavaScriptCore'):
            self.assertIn(expected, link.args)

    def test_codesign_annotation_from_post_build(self):
        graph = ninja.load(self.dir)
        jsc = graph.targets['JavaScriptCore']
        assert jsc.codesign_entitlements_source is not None
        self.assertEqual(jsc.codesign_entitlements_source.name,
                         'JavaScriptCore.xcent')
        self.assertEqual(jsc.codesign_identity, '-')

    def test_static_library_uses_libtool(self):
        graph = ninja.load(self.dir)
        self.assertEqual(graph.products()['libANGLE.a'].kind,
                         ProductKind.STATIC_LIB)
        link = graph.targets['ANGLE'].link_step
        assert link is not None
        self.assertEqual(link.linker, 'libtool')


class SplitLinkRuleTest(TestCase):
    def test_various_rule_names(self):
        self.assertEqual(
            ninja._split_link_rule('CXX_SHARED_LIBRARY_LINKER__JavaScriptCore_Debug'),
            'JavaScriptCore')
        self.assertEqual(
            ninja._split_link_rule('CXX_STATIC_LIBRARY_LINKER__ANGLE_Debug'),
            'ANGLE')
        self.assertEqual(
            ninja._split_link_rule('C_EXECUTABLE_LINKER__Test_WGSL_Debug'),
            'Test_WGSL')
        self.assertIsNone(ninja._split_link_rule('CXX_COMPILER__WTF_Debug'))
