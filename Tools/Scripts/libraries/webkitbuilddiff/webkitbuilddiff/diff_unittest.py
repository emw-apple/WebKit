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

from webkitbuilddiff import diff, report
from webkitbuilddiff.argnorm import Relativizer
from webkitbuilddiff.model import (
    BuildGraph, BuildSystem, CompileStep, LinkStep, Product, ProductKind, Target,
)


def _target(name, identity, kind, output, compile_args=(), link_args=()):
    target = Target(name=name)
    target.products.append(Product(identity=identity, kind=kind,
                                    output_path=Path(output), target_name=name))
    if compile_args:
        target.compile_steps.append(CompileStep(source=None, output=None,
                                                 args=list(compile_args)))
    target.link_step = LinkStep(product_identity=identity, linker='clang++',
                                args=list(link_args))
    return target


class DiffBinariesTest(TestCase):
    def setUp(self):
        self.xcode = BuildGraph(build_system=BuildSystem.XCODE, root=Path('/x'))
        self.ninja = BuildGraph(build_system=BuildSystem.NINJA, root=Path('/n'))
        self.xcode.targets['ANGLE'] = _target(
            'ANGLE (dynamic)', 'libANGLE-shared.dylib', ProductKind.DYLIB,
            '/x/libANGLE-shared.dylib')
        self.xcode.targets['JSC'] = _target(
            'JavaScriptCore', 'JavaScriptCore.framework', ProductKind.FRAMEWORK,
            '/x/JavaScriptCore.framework/Versions/A/JavaScriptCore')
        self.ninja.targets['ANGLE'] = _target(
            'ANGLE', 'libANGLE.a', ProductKind.STATIC_LIB, '/n/libANGLE.a')
        self.ninja.targets['JSC'] = _target(
            'JavaScriptCore', 'JavaScriptCore.framework', ProductKind.FRAMEWORK,
            '/n/JavaScriptCore.framework/Versions/A/JavaScriptCore')

    def test_only_and_common(self):
        result = diff.diff_binaries(self.xcode, self.ninja)
        self.assertEqual([p.identity for p in result.only_xcode],
                         ['libANGLE-shared.dylib'])
        self.assertEqual([p.identity for p in result.only_ninja], ['libANGLE.a'])
        self.assertEqual(result.common, ['JavaScriptCore.framework'])
        self.assertEqual(result.kind_mismatch, [])

    def test_kind_mismatch(self):
        # Make a shared identity differ only in kind.
        self.ninja.targets['gtest'] = _target(
            'gtest', 'gtest.framework', ProductKind.BUNDLE, '/n/gtest.framework')
        self.xcode.targets['gtest'] = _target(
            'gtest', 'gtest.framework', ProductKind.FRAMEWORK,
            '/x/gtest.framework/Versions/A/gtest')
        result = diff.diff_binaries(self.xcode, self.ninja)
        self.assertEqual(len(result.kind_mismatch), 1)
        self.assertEqual(result.kind_mismatch[0][0].kind, ProductKind.FRAMEWORK)


class DiffCompilerTest(TestCase):
    def test_global_categories_and_summary(self):
        xcode = BuildGraph(build_system=BuildSystem.XCODE, root=Path('/x'))
        ninja = BuildGraph(build_system=BuildSystem.NINJA, root=Path('/n'))
        xcode.targets['JSC'] = _target(
            'libJavaScriptCore', 'libJavaScriptCore.a', ProductKind.STATIC_LIB,
            '/x/libJavaScriptCore.a',
            compile_args=['-std=c++2b', '-O0', '-DONLY_XCODE', '-fmodules'])
        ninja.targets['JSC'] = _target(
            'JavaScriptCore', 'JavaScriptCore.framework', ProductKind.FRAMEWORK,
            '/n/JavaScriptCore.framework/Versions/A/JavaScriptCore',
            compile_args=['-std=c++23', '-Onone', '-DONLY_NINJA'])
        rel = Relativizer()
        result = diff.diff_compiler(xcode, ninja, rel, rel)
        defines = next(c for c in result.global_categories
                       if c.category == 'defines')
        self.assertEqual(defines.only_xcode, {'-DONLY_XCODE'})
        self.assertEqual(defines.only_ninja, {'-DONLY_NINJA'})
        xsummary = result.xcode_summary
        nsummary = result.ninja_summary
        assert xsummary is not None and nsummary is not None
        # -std=c++2b and c++23, -O0 and -Onone canonicalize equal.
        self.assertEqual(xsummary.cxx_std, nsummary.cxx_std)
        self.assertEqual(xsummary.optimization, nsummary.optimization)
        # modules differ.
        self.assertTrue(xsummary.modules)
        self.assertFalse(nsummary.modules)

    def test_per_target_matched_by_normalized_name(self):
        xcode = BuildGraph(build_system=BuildSystem.XCODE, root=Path('/x'))
        ninja = BuildGraph(build_system=BuildSystem.NINJA, root=Path('/n'))
        xcode.targets['ANGLE (dynamic)'] = _target(
            'ANGLE (dynamic)', 'libANGLE-shared.dylib', ProductKind.DYLIB,
            '/x/libANGLE-shared.dylib', compile_args=['-DX'])
        ninja.targets['ANGLE'] = _target(
            'ANGLE', 'libANGLE.a', ProductKind.STATIC_LIB, '/n/libANGLE.a',
            compile_args=['-DN'])
        rel = Relativizer()
        result = diff.diff_compiler(xcode, ninja, rel, rel)
        names = {t.name for t in result.per_target}
        self.assertIn('angle', names)


class DiffLinkersTest(TestCase):
    def test_linker_flag_difference(self):
        xcode = BuildGraph(build_system=BuildSystem.XCODE, root=Path('/x'))
        ninja = BuildGraph(build_system=BuildSystem.NINJA, root=Path('/n'))
        xcode.targets['JSC'] = _target(
            'JavaScriptCore', 'JavaScriptCore.framework', ProductKind.FRAMEWORK,
            '/x/JavaScriptCore.framework/Versions/A/JavaScriptCore',
            link_args=['-dynamiclib', '-framework', 'Foundation',
                       '-target', 'arm64e-apple-macos26.6'])
        ninja.targets['JSC'] = _target(
            'JavaScriptCore', 'JavaScriptCore.framework', ProductKind.FRAMEWORK,
            '/n/JavaScriptCore.framework/Versions/A/JavaScriptCore',
            link_args=['-dynamiclib', '-framework', 'Security',
                       '-arch', 'arm64e'])
        rel = Relativizer()
        result = diff.diff_linkers(xcode, ninja, ['JavaScriptCore.framework'],
                                   rel, rel)
        self.assertEqual(len(result), 1)
        linker = next(c for c in result[0].categories if c.category == 'linker')
        self.assertIn('-framework Foundation', linker.only_xcode)
        self.assertIn('-framework Security', linker.only_ninja)
        # -target vs -arch equivalence is stripped, so no codegen difference.
        self.assertNotIn('codegen', {c.category for c in result[0].categories})


class NormalizeTargetNameTest(TestCase):
    def test_collapses_qualifiers_and_lib_prefix(self):
        self.assertEqual(diff.normalize_target_name('ANGLE (dynamic)'), 'angle')
        self.assertEqual(diff.normalize_target_name('libJavaScriptCore'),
                         'javascriptcore')
        self.assertEqual(diff.normalize_target_name('WebCore'), 'webcore')


class RenderTest(TestCase):
    def test_text_and_json_render(self):
        xcode = BuildGraph(build_system=BuildSystem.XCODE, root=Path('/x'))
        ninja = BuildGraph(build_system=BuildSystem.NINJA, root=Path('/n'))
        xcode.targets['A'] = _target('A', 'a.dylib', ProductKind.DYLIB,
                                     '/x/a.dylib')
        result = diff.compare(xcode, ninja, arch='arm64e', repo_root=None,
                              dimensions=['targets'])
        text = report.render_text(result)
        self.assertIn('Comparing Xcode vs Ninja builds', text)
        self.assertIn('- a.dylib', text)
        self.assertTrue(result.has_differences())
        payload = report.to_json_dict(result)
        self.assertEqual(payload['arch'], 'arm64e')
        self.assertEqual(payload['binaries']['only_xcode'][0]['identity'],
                         'a.dylib')

    def test_html_render_is_self_contained(self):
        xcode = BuildGraph(build_system=BuildSystem.XCODE, root=Path('/x'))
        ninja = BuildGraph(build_system=BuildSystem.NINJA, root=Path('/n'))
        xcode.targets['A'] = _target('A', 'a.dylib', ProductKind.DYLIB,
                                     '/x/a.dylib')
        result = diff.compare(xcode, ninja, arch='arm64e', repo_root=None,
                              dimensions=['targets'])
        page = report.render_html(result)
        self.assertTrue(page.startswith('<!DOCTYPE html>'))
        # Everything is inline: no external stylesheet/script references.
        self.assertNotIn('<link', page)
        self.assertNotIn('src=', page)
        self.assertIn('<style>', page)
        self.assertIn('a.dylib', page)

    def test_html_escapes_content(self):
        xcode = BuildGraph(build_system=BuildSystem.XCODE, root=Path('/x'))
        ninja = BuildGraph(build_system=BuildSystem.NINJA, root=Path('/n'))
        xcode.targets['A'] = _target(
            'A', 'a.dylib', ProductKind.DYLIB, '/x/a.dylib',
            link_args=['-DEVIL=<script>x</script>'])
        ninja.targets['A'] = _target('A', 'a.dylib', ProductKind.DYLIB,
                                     '/n/a.dylib')
        result = diff.compare(xcode, ninja, arch='arm64e', repo_root=None,
                              dimensions=['targets', 'args'])
        page = report.render_html(result)
        self.assertNotIn('<script>x</script>', page)
        self.assertIn('&lt;script&gt;x&lt;/script&gt;', page)
