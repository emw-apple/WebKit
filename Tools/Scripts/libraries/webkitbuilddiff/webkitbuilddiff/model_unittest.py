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

from webkitbuilddiff.model import (
    BuildGraph, BuildSystem, LinkStep, Product, ProductKind, Target,
    classify_product,
)


class ClassifyProductTest(TestCase):
    def test_framework_binary(self):
        self.assertEqual(
            classify_product(Path('/b/JavaScriptCore.framework/Versions/A/JavaScriptCore')),
            ('JavaScriptCore.framework', ProductKind.FRAMEWORK))

    def test_nested_dylib_is_its_own_product(self):
        self.assertEqual(
            classify_product(Path('/b/WebKit.framework/Versions/A/Frameworks/libWebKitSwift.dylib')),
            ('libWebKitSwift.dylib', ProductKind.DYLIB))

    def test_xpc_service_does_not_collide_with_framework(self):
        path = Path('/b/WebKit.framework/Versions/A/XPCServices/'
                    'com.apple.WebKit.GPU.xpc/Contents/MacOS/GPU.Development')
        self.assertEqual(classify_product(path),
                         ('com.apple.WebKit.GPU.xpc', ProductKind.BUNDLE))

    def test_app_bundle(self):
        self.assertEqual(
            classify_product(Path('/b/MiniBrowser.app/Contents/MacOS/MiniBrowser')),
            ('MiniBrowser.app', ProductKind.APP_BUNDLE))

    def test_static_lib_and_dylib_and_executable(self):
        self.assertEqual(classify_product(Path('/b/libANGLE.a')),
                         ('libANGLE.a', ProductKind.STATIC_LIB))
        self.assertEqual(classify_product(Path('/b/libwebrtc.dylib')),
                         ('libwebrtc.dylib', ProductKind.DYLIB))
        self.assertEqual(classify_product(Path('/b/jsc')),
                         ('jsc', ProductKind.EXECUTABLE))


class BuildGraphTest(TestCase):
    def _graph(self):
        graph = BuildGraph(build_system=BuildSystem.NINJA, root=Path('/b'))
        target = Target(name='JavaScriptCore')
        target.products.append(Product(
            identity='JavaScriptCore.framework', kind=ProductKind.FRAMEWORK,
            output_path=Path('/b/JavaScriptCore.framework/Versions/A/JavaScriptCore'),
            target_name='JavaScriptCore'))
        target.link_step = LinkStep(product_identity='JavaScriptCore.framework')
        graph.targets['JavaScriptCore'] = target
        return graph

    def test_products_and_targets_by_product(self):
        graph = self._graph()
        self.assertIn('JavaScriptCore.framework', graph.products())
        self.assertEqual(
            graph.targets_by_product()['JavaScriptCore.framework'].name,
            'JavaScriptCore')
