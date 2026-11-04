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
from webkitbuilddiff.argnorm import CompileSummary, Relativizer, SwiftSummary
from webkitbuilddiff.model import (
    BuildGraph, BuildSystem, CompileStep, Language, LinkStep, Product,
    ProductKind, Target,
)


def _target(name, identity, kind, output, compile_args=(), link_args=(),
            language=Language.CLANG):
    target = Target(name=name)
    target.products.append(Product(identity=identity, kind=kind,
                                    output_path=Path(output), target_name=name))
    if compile_args:
        target.compile_steps.append(CompileStep(
            source=None, output=None, args=list(compile_args),
            language=language))
    target.link_step = LinkStep(product_identity=identity, linker='clang++',
                                args=list(link_args))
    return target


def _graph(system, root):
    return BuildGraph(build_system=system, root=Path(root))


class DiffBinariesTest(TestCase):
    def setUp(self):
        self.xcode = _graph(BuildSystem.XCODE, '/x')
        self.ninja = _graph(BuildSystem.NINJA, '/n')
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
        self.assertEqual([p.identity for p in result.only_a],
                         ['libANGLE-shared.dylib'])
        self.assertEqual([p.identity for p in result.only_b], ['libANGLE.a'])
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
        xcode = _graph(BuildSystem.XCODE, '/x')
        ninja = _graph(BuildSystem.NINJA, '/n')
        xcode.targets['JSC'] = _target(
            'libJavaScriptCore', 'libJavaScriptCore.a', ProductKind.STATIC_LIB,
            '/x/libJavaScriptCore.a',
            compile_args=['-std=c++2b', '-O0', '-DONLY_XCODE', '-fmodules'])
        ninja.targets['JSC'] = _target(
            'JavaScriptCore', 'JavaScriptCore.framework', ProductKind.FRAMEWORK,
            '/n/JavaScriptCore.framework/Versions/A/JavaScriptCore',
            compile_args=['-std=c++23', '-Onone', '-DONLY_NINJA'])
        rel = Relativizer()
        result = diff.diff_compiler(xcode, ninja, rel, rel, a_label='Xcode',
                                    b_label='Ninja', cross_system=True)
        clang = result.clang
        assert clang is not None
        self.assertIsNone(result.swift)  # no Swift steps in this fixture
        defines = next(c for c in clang.global_categories
                       if c.category == 'defines')
        self.assertEqual(defines.only_a, {'-DONLY_XCODE'})
        self.assertEqual(defines.only_b, {'-DONLY_NINJA'})
        asummary = clang.a_summary
        bsummary = clang.b_summary
        assert isinstance(asummary, CompileSummary)
        assert isinstance(bsummary, CompileSummary)
        # -std=c++2b and c++23, -O0 and -Onone canonicalize equal.
        self.assertEqual(asummary.cxx_std, bsummary.cxx_std)
        self.assertEqual(asummary.optimization, bsummary.optimization)
        # modules differ.
        self.assertTrue(asummary.modules)
        self.assertFalse(bsummary.modules)

    def test_swift_and_clang_kept_separate(self):
        xcode = _graph(BuildSystem.XCODE, '/x')
        ninja = _graph(BuildSystem.NINJA, '/n')
        # Both builds compile clang; only Ninja records Swift args.
        xcode.targets['PAL'] = _target(
            'PAL', 'libPAL.a', ProductKind.STATIC_LIB, '/x/libPAL.a',
            compile_args=['-DENABLE_X=1', '-O0'])
        pal = _target('PAL', 'libPAL.a', ProductKind.STATIC_LIB, '/n/libPAL.a',
                      compile_args=['-DENABLE_X=1', '-O0'])
        pal.compile_steps.append(CompileStep(
            source=Path('/s/pal.swift'), output=None,
            args=['-DBUILDING_PAL', '-Onone', '-swift-version', '5', '-wmo'],
            language=Language.SWIFT))
        ninja.targets['PAL'] = pal
        rel = Relativizer()
        result = diff.diff_compiler(xcode, ninja, rel, rel, a_label='Xcode',
                                    b_label='Ninja', cross_system=True)
        assert result.clang is not None and result.swift is not None
        # The clang comparison must not see any Swift tokens.
        self.assertNotIn('conditionals',
                         {c.category for c in result.clang.global_categories})
        clang_flags = set()
        for c in result.clang.global_categories:
            clang_flags |= c.only_b
        self.assertNotIn('-Onone', clang_flags)
        self.assertNotIn('-DBUILDING_PAL', clang_flags)
        # Swift-only build => note explains Xcode's missing Swift args.
        self.assertIsNotNone(result.swift.note)
        self.assertIn('Xcode', result.swift.note or '')
        self.assertIsNone(result.swift.a_summary)
        bswift = result.swift.b_summary
        assert isinstance(bswift, SwiftSummary)
        self.assertTrue(bswift.wmo)
        self.assertEqual(bswift.swift_version, '5')
        # Swift '-D' is a conditional, categorized separately from clang defines.
        swift_conditionals = next(
            c for c in result.swift.global_categories
            if c.category == 'conditionals')
        self.assertIn('-DBUILDING_PAL', swift_conditionals.only_b)

    def test_per_target_matched_by_normalized_name(self):
        xcode = _graph(BuildSystem.XCODE, '/x')
        ninja = _graph(BuildSystem.NINJA, '/n')
        xcode.targets['ANGLE (dynamic)'] = _target(
            'ANGLE (dynamic)', 'libANGLE-shared.dylib', ProductKind.DYLIB,
            '/x/libANGLE-shared.dylib', compile_args=['-DX'])
        ninja.targets['ANGLE'] = _target(
            'ANGLE', 'libANGLE.a', ProductKind.STATIC_LIB, '/n/libANGLE.a',
            compile_args=['-DN'])
        rel = Relativizer()
        result = diff.diff_compiler(xcode, ninja, rel, rel, a_label='Xcode',
                                    b_label='Ninja', cross_system=True)
        assert result.clang is not None
        names = {t.name for t in result.clang.per_target}
        self.assertIn('angle', names)


class DiffLinkersTest(TestCase):
    def test_linker_flag_difference(self):
        xcode = _graph(BuildSystem.XCODE, '/x')
        ninja = _graph(BuildSystem.NINJA, '/n')
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
                                   rel, rel, cross_system=True)
        self.assertEqual(len(result), 1)
        linker = next(c for c in result[0].categories if c.category == 'linker')
        self.assertIn('-framework Foundation', linker.only_a)
        self.assertIn('-framework Security', linker.only_b)
        # -target vs -arch equivalence is stripped, so no codegen difference.
        self.assertNotIn('codegen', {c.category for c in result[0].categories})


class SameSystemDiffTest(TestCase):
    """Two builds from the same build system (e.g. two CMake configurations)."""

    def _compare(self, a_args, b_args):
        a = _graph(BuildSystem.NINJA, '/a')
        b = _graph(BuildSystem.NINJA, '/b')
        a.targets['T'] = _target('T', 'libT.a', ProductKind.STATIC_LIB,
                                  '/a/libT.a', compile_args=a_args)
        b.targets['T'] = _target('T', 'libT.a', ProductKind.STATIC_LIB,
                                  '/b/libT.a', compile_args=b_args)
        return diff.compare(a, b, arch='arm64e', repo_root=None,
                            dimensions=['args'], a_label='main', b_label='mine')

    def _clang_flags(self, result):
        clang = result.compiler.clang
        assert clang is not None
        only_a: set[str] = set()
        only_b: set[str] = set()
        for c in clang.global_categories:
            only_a |= c.only_a
            only_b |= c.only_b
        return only_a, only_b

    def test_surfaces_deployment_target_change(self):
        # Same system: a genuine deployment-target change must NOT be stripped.
        result = self._compare(
            ['-O2', '-mmacosx-version-min=13.0'],
            ['-O2', '-mmacosx-version-min=14.0'])
        only_a, only_b = self._clang_flags(result)
        self.assertTrue(any('-mmacosx-version-min=13' in f for f in only_a))
        self.assertTrue(any('-mmacosx-version-min=14' in f for f in only_b))

    def test_cross_system_strips_deployment_target(self):
        # Cross system: the same tokens are equal-by-construction noise.
        a = _graph(BuildSystem.XCODE, '/x')
        b = _graph(BuildSystem.NINJA, '/n')
        a.targets['T'] = _target('T', 'libT.a', ProductKind.STATIC_LIB,
                                  '/x/libT.a',
                                  compile_args=['-O2', '-mmacosx-version-min=13.0'])
        b.targets['T'] = _target('T', 'libT.a', ProductKind.STATIC_LIB,
                                  '/n/libT.a',
                                  compile_args=['-O2', '-mmacosx-version-min=14.0'])
        result = diff.compare(a, b, arch='arm64e', repo_root=None,
                              dimensions=['args'], a_label='Xcode',
                              b_label='Ninja')
        only_a, only_b = self._clang_flags(result)
        self.assertFalse(any('mmacosx-version-min' in f
                             for f in only_a | only_b))

    def test_no_swift_note_when_same_system(self):
        a = _graph(BuildSystem.NINJA, '/a')
        b = _graph(BuildSystem.NINJA, '/b')
        for graph, root, define in ((a, '/a', 'A'), (b, '/b', 'B')):
            t = _target('T', 'libT.a', ProductKind.STATIC_LIB, f'{root}/libT.a')
            t.compile_steps.append(CompileStep(
                source=Path(f'{root}/t.swift'), output=None,
                args=[f'-D{define}', '-wmo'], language=Language.SWIFT))
            graph.targets['T'] = t
        result = diff.compare(a, b, arch='arm64e', repo_root=None,
                              dimensions=['args'], a_label='main', b_label='mine')
        assert result.compiler is not None and result.compiler.swift is not None
        # Same-system Swift comparison is symmetric: no Xcode-manifest note.
        self.assertIsNone(result.compiler.swift.note)
        self.assertIsNotNone(result.compiler.swift.a_summary)
        self.assertIsNotNone(result.compiler.swift.b_summary)


class NormalizeTargetNameTest(TestCase):
    def test_collapses_qualifiers_and_lib_prefix(self):
        self.assertEqual(diff.normalize_target_name('ANGLE (dynamic)'), 'angle')
        self.assertEqual(diff.normalize_target_name('libJavaScriptCore'),
                         'javascriptcore')
        self.assertEqual(diff.normalize_target_name('WebCore'), 'webcore')


class RenderTest(TestCase):
    def test_text_and_json_render(self):
        xcode = _graph(BuildSystem.XCODE, '/x')
        ninja = _graph(BuildSystem.NINJA, '/n')
        xcode.targets['A'] = _target('A', 'a.dylib', ProductKind.DYLIB,
                                     '/x/a.dylib')
        result = diff.compare(xcode, ninja, arch='arm64e', repo_root=None,
                              dimensions=['targets'], a_label='Xcode',
                              b_label='Ninja')
        text = report.render_text(result)
        self.assertIn('Comparing Xcode vs Ninja builds', text)
        self.assertIn('- a.dylib', text)
        self.assertTrue(result.has_differences())
        payload = report.to_json_dict(result)
        self.assertEqual(payload['arch'], 'arm64e')
        self.assertEqual(payload['a_label'], 'Xcode')
        self.assertEqual(payload['b_label'], 'Ninja')
        self.assertEqual(payload['binaries']['only_a'][0]['identity'],
                         'a.dylib')

    def test_labels_flow_to_report_for_same_system(self):
        a = _graph(BuildSystem.NINJA, '/main')
        b = _graph(BuildSystem.NINJA, '/mine')
        a.targets['A'] = _target('A', 'a.dylib', ProductKind.DYLIB,
                                 '/main/a.dylib')
        result = diff.compare(a, b, arch='arm64e', repo_root=None,
                              dimensions=['targets'], a_label='main',
                              b_label='mine')
        text = report.render_text(result)
        self.assertIn('Comparing main vs mine builds', text)
        self.assertIn('only main', text)
        page = report.render_html(result)
        self.assertIn('main vs mine build diff', page)

    def test_html_render_is_self_contained(self):
        xcode = _graph(BuildSystem.XCODE, '/x')
        ninja = _graph(BuildSystem.NINJA, '/n')
        xcode.targets['A'] = _target('A', 'a.dylib', ProductKind.DYLIB,
                                     '/x/a.dylib')
        result = diff.compare(xcode, ninja, arch='arm64e', repo_root=None,
                              dimensions=['targets'], a_label='Xcode',
                              b_label='Ninja')
        page = report.render_html(result)
        self.assertTrue(page.startswith('<!DOCTYPE html>'))
        # Everything is inline: no external stylesheet/script references.
        self.assertNotIn('<link', page)
        self.assertNotIn('src=', page)
        self.assertIn('<style>', page)
        self.assertIn('a.dylib', page)

    def test_html_escapes_content(self):
        xcode = _graph(BuildSystem.XCODE, '/x')
        ninja = _graph(BuildSystem.NINJA, '/n')
        xcode.targets['A'] = _target(
            'A', 'a.dylib', ProductKind.DYLIB, '/x/a.dylib',
            link_args=['-DEVIL=<script>x</script>'])
        ninja.targets['A'] = _target('A', 'a.dylib', ProductKind.DYLIB,
                                     '/n/a.dylib')
        result = diff.compare(xcode, ninja, arch='arm64e', repo_root=None,
                              dimensions=['targets', 'args'], a_label='Xcode',
                              b_label='Ninja')
        page = report.render_html(result)
        self.assertNotIn('<script>x</script>', page)
        self.assertIn('&lt;script&gt;x&lt;/script&gt;', page)
