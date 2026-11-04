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

from pathlib import Path
from unittest import TestCase

from webkitbuilddiff.argnorm import (
    Relativizer, categorize, categorize_swift, normalize_args,
    summarize_compile, summarize_swift,
)
from webkitbuilddiff.model import Language


class RelativizerTest(TestCase):
    def setUp(self):
        # The build root is nested under the repo root; the more specific label
        # must win.
        self.rel = Relativizer.create(
            repo_root=Path('/repo'),
            build_root=Path('/repo/WebKitBuild/Debug'))

    def test_most_specific_root_wins(self):
        self.assertEqual(self.rel.path('/repo/WebKitBuild/Debug/x'), '$BUILD/x')
        self.assertEqual(self.rel.path('/repo/Source/y'), '$SRC/Source/y')

    def test_joined_include_path(self):
        self.assertEqual(self.rel.token('-I/repo/Source/z'), '-I$SRC/Source/z')

    def test_wl_comma_path(self):
        self.assertEqual(
            self.rel.token('-Wl,-order_file,/repo/WebKitBuild/Debug/o.txt'),
            '-Wl,-order_file,$BUILD/o.txt')

    def test_unknown_path_untouched(self):
        self.assertEqual(self.rel.path('/opt/other'), '/opt/other')


class NormalizeArgsTest(TestCase):
    def test_drops_positional_inputs_and_output(self):
        args = ['/tc/clang++', '-DFOO', '-c', '/src/a.cpp', '-o', 'a.o',
                'b.o', '@objs.rsp']
        result = normalize_args(args)
        self.assertEqual(result.all_flags(), {'-DFOO'})

    def test_separate_value_flag_relativized(self):
        rel = Relativizer.create(build_root=Path('/b'))
        result = normalize_args(['-isystem', '/b/inc'], rel)
        self.assertEqual(result.by_category['includes'], {'-isystem $BUILD/inc'})

    def test_drops_diagnostic_noise(self):
        result = normalize_args(['-fcolor-diagnostics', '-fdiagnostics-color=always',
                                 '-Wall'])
        self.assertEqual(result.all_flags(), {'-Wall'})

    def test_categorize(self):
        self.assertEqual(categorize('-DFOO'), 'defines')
        self.assertEqual(categorize('-I/x'), 'includes')
        self.assertEqual(categorize('-F/x'), 'includes')
        self.assertEqual(categorize('-framework Foundation'), 'linker')
        self.assertEqual(categorize('-Wall'), 'warnings')
        self.assertEqual(categorize('-Wl,-dead_strip'), 'linker')
        self.assertEqual(categorize('-std=c++2b'), 'language')
        self.assertEqual(categorize('-O2'), 'optimization')
        self.assertEqual(categorize('-g'), 'debug')
        self.assertEqual(categorize('-fno-exceptions'), 'codegen')
        self.assertEqual(categorize('-isysroot /SDK'), 'sdk')


class SummarizeCompileTest(TestCase):
    def test_canonicalizes_equivalent_spellings(self):
        summary = summarize_compile(
            ['-Onone', '-std=c++2b', '-g', '-stdlib=libc++'], tu_count=3)
        self.assertEqual(summary.optimization, '-O0')
        self.assertEqual(summary.cxx_std, 'c++23')
        self.assertTrue(summary.debug_info)
        self.assertEqual(summary.stdlib, 'libc++')
        self.assertEqual(summary.tu_count, 3)

    def test_sanitizers_and_modules(self):
        summary = summarize_compile(
            ['-fsanitize=address,undefined', '-fmodules'], tu_count=1)
        self.assertEqual(summary.sanitizers, ['address', 'undefined'])
        self.assertTrue(summary.modules)

    def test_objc_mode_flag_not_mistaken_for_optimization(self):
        # -ObjC++ starts with -O but is a language mode, not an opt level.
        summary = summarize_compile(['-ObjC++', '-O2'], tu_count=1)
        self.assertEqual(summary.optimization, '-O2')
        self.assertEqual(categorize('-ObjC++'), 'misc')
        self.assertEqual(categorize('-O2'), 'optimization')
        self.assertEqual(categorize('-O'), 'optimization')


class SwiftNormalizeTest(TestCase):
    def test_swift_categorization(self):
        # Swift semantics differ from clang: -D is a conditional, -Xcc forwards
        # to clang, -O family are Swift optimization modes.
        self.assertEqual(categorize_swift('-DBUILDING_PAL'), 'conditionals')
        self.assertEqual(categorize_swift('-Onone'), 'optimization')
        self.assertEqual(categorize_swift('-Xcc -DFOO'), 'clang-passthrough')
        self.assertEqual(categorize_swift('-enable-library-evolution'),
                         'features')
        self.assertEqual(categorize_swift('-swift-version 5'), 'language')
        self.assertEqual(categorize_swift('-I/x'), 'search')
        self.assertEqual(categorize_swift('-module-name PAL'), 'module')

    def test_swift_normalize_drops_parallelism_and_outputs(self):
        rel = Relativizer.create(build_root=Path('/b'))
        args = ['/tc/swiftc', '-c', '-num-threads', '16', '-j', '16',
                '-target', 'arm64e-apple-macosx26.6', '-DBUILDING_PAL',
                '-o', '/b/pal.o', '-primary-file', '/s/pal.swift',
                '/s/pal.swift', '-I', '/b/inc']
        result = normalize_args(args, rel, Language.SWIFT)
        flags = result.all_flags()
        self.assertIn('-DBUILDING_PAL', flags)
        self.assertIn('-I $BUILD/inc', flags)
        self.assertIn('-target arm64e-apple-macosx26.6', flags)
        # parallelism, mode, output path, and sources are dropped
        self.assertNotIn('-num-threads 16', flags)
        self.assertNotIn('-o /b/pal.o', flags)
        self.assertFalse(any('pal.swift' in f for f in flags))

    def test_summarize_swift(self):
        summary = summarize_swift(
            ['-Onone', '-g', '-swift-version', '5', '-wmo',
             '-enable-library-evolution'], tu_count=4)
        self.assertEqual(summary.optimization, '-Onone')
        self.assertTrue(summary.debug_info)
        self.assertEqual(summary.swift_version, '5')
        self.assertTrue(summary.wmo)
        self.assertTrue(summary.library_evolution)
        self.assertEqual(summary.tu_count, 4)
