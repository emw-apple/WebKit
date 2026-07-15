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
    Relativizer, categorize, normalize_args, summarize_compile,
)


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
