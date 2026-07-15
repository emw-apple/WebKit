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
"""Build-system-agnostic data model for the Xcode/Ninja build comparison.

Both the Xcode (``xcode.py``) and Ninja (``ninja.py``) extractors produce the
same :class:`BuildGraph` shape so that :mod:`webkitbuilddiff.diff` can compare
them without caring which build system they came from.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Optional


class BuildSystem(str, Enum):
    XCODE = 'xcode'
    NINJA = 'ninja'

    def __str__(self) -> str:
        return self.value


class ProductKind(str, Enum):
    FRAMEWORK = 'framework'
    DYLIB = 'dylib'
    STATIC_LIB = 'static_lib'
    EXECUTABLE = 'executable'
    APP_BUNDLE = 'app_bundle'
    BUNDLE = 'bundle'
    UNKNOWN = 'unknown'

    def __str__(self) -> str:
        return self.value


@dataclass(frozen=True)
class Product:
    """A linkable/bundled artifact a build produces.

    ``identity`` is the stable key used to join the two builds. It is the leaf
    product name (e.g. ``JavaScriptCore.framework``, ``libwebrtc.dylib``,
    ``jsc``) rather than a target name, because Xcode target names ("ANGLE
    (dynamic)") and Ninja target names ("ANGLE") do not match textually.
    """
    identity: str
    kind: ProductKind
    output_path: Path
    target_name: str


@dataclass
class CompileStep:
    """One translation unit's compilation."""
    source: Optional[Path]
    output: Optional[Path]
    args: list[str] = field(default_factory=list)


@dataclass
class LinkStep:
    """The link (or archive) invocation that produces a target's product."""
    product_identity: str
    linker: Optional[str] = None
    args: list[str] = field(default_factory=list)


@dataclass
class Target:
    name: str
    products: list[Product] = field(default_factory=list)
    compile_steps: list[CompileStep] = field(default_factory=list)
    link_step: Optional[LinkStep] = None
    # Intended codesigning as recorded in the build graph. The signing identity
    # is frequently absent from the graph (ground truth is the produced binary,
    # inspected in artifacts.py); these fields are best-effort annotations only.
    codesign_entitlements_source: Optional[Path] = None
    codesign_identity: Optional[str] = None


@dataclass
class BuildGraph:
    build_system: BuildSystem
    root: Path
    targets: dict[str, Target] = field(default_factory=dict)

    def products(self) -> dict[str, Product]:
        """Map every product identity produced by this build to its Product."""
        out: dict[str, Product] = {}
        for target in self.targets.values():
            for product in target.products:
                out[product.identity] = product
        return out

    def targets_by_product(self) -> dict[str, Target]:
        """Map every product identity to the Target that produces it."""
        out: dict[str, Target] = {}
        for target in self.targets.values():
            for product in target.products:
                out[product.identity] = target
        return out


@dataclass
class CodesignInfo:
    """Ground-truth signing state read from a produced binary via ``codesign -d``."""
    identifier: Optional[str] = None
    authority: list[str] = field(default_factory=list)
    flags: Optional[str] = None
    team_id: Optional[str] = None
    format: Optional[str] = None
    entitlements: dict[str, object] = field(default_factory=dict)
    signed: bool = True


# File extensions that identify a product directly by its basename.
_BASENAME_KINDS = (
    ('.dylib', ProductKind.DYLIB),
    ('.a', ProductKind.STATIC_LIB),
)

# Bundle directory extensions, checked innermost-first so a helper nested inside
# a framework (e.g. an .xpc service) is treated as its own product rather than
# colliding with the enclosing framework's identity.
_BUNDLE_KINDS = (
    ('.framework', ProductKind.FRAMEWORK),
    ('.app', ProductKind.APP_BUNDLE),
    ('.xpc', ProductKind.BUNDLE),
    ('.appex', ProductKind.BUNDLE),
    ('.bundle', ProductKind.BUNDLE),
)


def classify_product(output_path: Path) -> tuple[str, ProductKind]:
    """Derive a build-system-agnostic ``(identity, kind)`` from a product path.

    Examples::

        .../JavaScriptCore.framework/Versions/A/JavaScriptCore -> (JavaScriptCore.framework, FRAMEWORK)
        .../WebKit.framework/.../Frameworks/libWebKitSwift.dylib -> (libWebKitSwift.dylib, DYLIB)
        .../WebKit.framework/.../XPCServices/com.apple.WebKit.GPU.xpc/Contents/MacOS/x -> (com.apple.WebKit.GPU.xpc, BUNDLE)
        .../libwebrtc.dylib                                     -> (libwebrtc.dylib, DYLIB)
        .../libANGLE.a                                          -> (libANGLE.a, STATIC_LIB)
        .../MiniBrowser.app/Contents/MacOS/MiniBrowser          -> (MiniBrowser.app, APP_BUNDLE)
        .../jsc                                                 -> (jsc, EXECUTABLE)
    """
    name = output_path.name
    # A basename with a linkable extension wins outright. This also handles the
    # nested-dylib case (a .dylib inside a .framework is its own product).
    for suffix, kind in _BASENAME_KINDS:
        if name.endswith(suffix):
            return name, kind
    # The path may itself be a bundle directory.
    for suffix, kind in _BUNDLE_KINDS:
        if name.endswith(suffix):
            return name, kind
    # A bare Mach-O: attribute it to the innermost enclosing bundle, if any.
    for part in reversed(output_path.parts):
        for suffix, kind in _BUNDLE_KINDS:
            if part.endswith(suffix):
                return part, kind
    # A standalone command-line executable.
    return name, ProductKind.EXECUTABLE
