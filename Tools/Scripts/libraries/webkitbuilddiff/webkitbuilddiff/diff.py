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
"""Compute differences between two :class:`~webkitbuilddiff.model.BuildGraph`\\ s.

Everything joins on :attr:`Product.identity` (the leaf product name), never on
target names, because the two build systems name and factor targets differently
(Xcode compiles JSC into an intermediate ``libJavaScriptCore.a``; Ninja links
the objects straight into ``JavaScriptCore.framework``). The compiler-flag diff
is therefore reported globally (all TUs) plus best-effort per-target, while
linker flags, symbols, and codesigning join cleanly per product.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Iterable, Optional

from webkitbuilddiff import artifacts
from webkitbuilddiff.argnorm import (
    CATEGORIES, CompileSummary, NormalizedArgs, Relativizer, normalize_args,
    summarize_compile,
)
from webkitbuilddiff.model import (
    BuildGraph, CodesignInfo, Product,
)

# Dimensions the tool can compare.
DIMENSIONS = ('targets', 'args', 'symbols', 'codesign')

# Compile/link flag tokens excluded from diffing: they are equal by construction
# (same SDK/arch) but spelled asymmetrically across the two build systems.
_COMPILE_IGNORED_PREFIXES = ('-arch', '-target', '-mmacosx-version-min',
                             '-mtargetos', '-isysroot')


@dataclass
class ArgCategoryDiff:
    category: str
    only_xcode: set[str] = field(default_factory=set)
    only_ninja: set[str] = field(default_factory=set)

    def any(self) -> bool:
        return bool(self.only_xcode or self.only_ninja)


@dataclass
class BinaryDiff:
    only_xcode: list[Product] = field(default_factory=list)
    only_ninja: list[Product] = field(default_factory=list)
    kind_mismatch: list[tuple[Product, Product]] = field(default_factory=list)
    common: list[str] = field(default_factory=list)


@dataclass
class TargetCompilerDiff:
    name: str
    categories: list[ArgCategoryDiff] = field(default_factory=list)
    xcode_summary: Optional[CompileSummary] = None
    ninja_summary: Optional[CompileSummary] = None

    def any(self) -> bool:
        return (bool(self.categories) or
                self.xcode_summary != self.ninja_summary)


@dataclass
class CompilerDiff:
    global_categories: list[ArgCategoryDiff] = field(default_factory=list)
    xcode_summary: Optional[CompileSummary] = None
    ninja_summary: Optional[CompileSummary] = None
    per_target: list[TargetCompilerDiff] = field(default_factory=list)


@dataclass
class LinkerDiff:
    identity: str
    categories: list[ArgCategoryDiff] = field(default_factory=list)


@dataclass
class SymbolDiff:
    identity: str
    xcode_count: int = 0
    ninja_count: int = 0
    common_count: int = 0
    only_xcode: set[str] = field(default_factory=set)
    only_ninja: set[str] = field(default_factory=set)
    error: Optional[str] = None


@dataclass
class CodesignDiff:
    identity: str
    xcode: Optional[CodesignInfo] = None
    ninja: Optional[CodesignInfo] = None
    entitlements_only_xcode: set[str] = field(default_factory=set)
    entitlements_only_ninja: set[str] = field(default_factory=set)
    entitlement_value_differs: set[str] = field(default_factory=set)
    error: Optional[str] = None

    def identifier_differs(self) -> bool:
        return bool(self.xcode and self.ninja and
                    self.xcode.identifier != self.ninja.identifier)

    def authority_differs(self) -> bool:
        return bool(self.xcode and self.ninja and
                    self.xcode.authority != self.ninja.authority)

    def flags_differ(self) -> bool:
        return bool(self.xcode and self.ninja and
                    self.xcode.flags != self.ninja.flags)

    def any(self) -> bool:
        return bool(self.error or self.identifier_differs() or
                    self.authority_differs() or self.flags_differ() or
                    self.entitlements_only_xcode or
                    self.entitlements_only_ninja or
                    self.entitlement_value_differs)


@dataclass
class DiffResult:
    arch: str
    xcode_root: str
    ninja_root: str
    dimensions: list[str]
    binaries: Optional[BinaryDiff] = None
    compiler: Optional[CompilerDiff] = None
    linkers: list[LinkerDiff] = field(default_factory=list)
    symbols: list[SymbolDiff] = field(default_factory=list)
    codesign: list[CodesignDiff] = field(default_factory=list)

    def has_differences(self) -> bool:
        b = self.binaries
        if b and (b.only_xcode or b.only_ninja or b.kind_mismatch):
            return True
        if self.compiler and (self.compiler.global_categories or
                              any(t.any() for t in self.compiler.per_target)):
            return True
        if self.linkers or any(s.only_xcode or s.only_ninja or s.error
                               for s in self.symbols):
            return True
        return any(c.any() for c in self.codesign)


def normalize_target_name(name: str) -> str:
    """Collapse build-system-specific target spellings to a common key.

    ``ANGLE (dynamic)`` and ``ANGLE`` -> ``angle``;
    ``libJavaScriptCore`` and ``JavaScriptCore`` -> ``javascriptcore``.
    """
    collapsed = re.sub(r'\s*\([^)]*\)', '', name).strip().lower()
    if collapsed.startswith('lib'):
        collapsed = collapsed[len('lib'):]
    return collapsed


def _diff_categories(xcode: NormalizedArgs,
                     ninja: NormalizedArgs) -> list[ArgCategoryDiff]:
    diffs: list[ArgCategoryDiff] = []
    for category in CATEGORIES:
        x = xcode.by_category.get(category, set())
        n = ninja.by_category.get(category, set())
        diff = ArgCategoryDiff(category=category,
                               only_xcode=x - n, only_ninja=n - x)
        if diff.any():
            diffs.append(diff)
    return diffs


def _strip_target_equivalence(normalized: NormalizedArgs) -> NormalizedArgs:
    """Drop SDK/arch/target flags that are equal by construction across builds.

    Both builds target the same arch and SDK, but spell it differently
    (``-target arm64e-apple-macos26.6`` vs ``-arch arm64e``), which would
    otherwise read as a difference in every product.
    """
    normalized.by_category.pop('sdk', None)
    for category, flags in list(normalized.by_category.items()):
        kept = {f for f in flags if not f.startswith(_COMPILE_IGNORED_PREFIXES)}
        if kept:
            normalized.by_category[category] = kept
        else:
            normalized.by_category.pop(category, None)
    return normalized


def _compile_normalized(args_iter: Iterable[list[str]],
                        relativizer: Relativizer) -> NormalizedArgs:
    """Normalize compile flags, dropping SDK/arch tokens known-equal by design."""
    merged = NormalizedArgs()
    for args in args_iter:
        merged.merge(normalize_args(args, relativizer))
    return _strip_target_equivalence(merged)


def diff_binaries(xcode: BuildGraph, ninja: BuildGraph) -> BinaryDiff:
    xprods = xcode.products()
    nprods = ninja.products()
    result = BinaryDiff()
    for identity in sorted(set(xprods) | set(nprods)):
        x = xprods.get(identity)
        n = nprods.get(identity)
        if x and not n:
            result.only_xcode.append(x)
        elif n and not x:
            result.only_ninja.append(n)
        elif x and n:
            if x.kind != n.kind:
                result.kind_mismatch.append((x, n))
            else:
                result.common.append(identity)
    return result


def _aggregate_compile_by_name(graph: BuildGraph
                               ) -> dict[str, tuple[list[list[str]], int]]:
    """Map normalized target name -> (list of TU arg-lists, TU count)."""
    out: dict[str, tuple[list[list[str]], int]] = {}
    for target in graph.targets.values():
        if not target.compile_steps:
            continue
        key = normalize_target_name(target.name)
        arg_lists, count = out.get(key, ([], 0))
        arg_lists = arg_lists + [step.args for step in target.compile_steps]
        out[key] = (arg_lists, count + len(target.compile_steps))
    return out


def diff_compiler(xcode: BuildGraph, ninja: BuildGraph,
                  xrel: Relativizer, nrel: Relativizer) -> CompilerDiff:
    result = CompilerDiff()

    # Global: every compile step across the whole build.
    x_all = [s.args for t in xcode.targets.values() for s in t.compile_steps]
    n_all = [s.args for t in ninja.targets.values() for s in t.compile_steps]
    result.global_categories = _diff_categories(
        _compile_normalized(x_all, xrel), _compile_normalized(n_all, nrel))
    result.xcode_summary = summarize_compile(
        (tok for args in x_all for tok in args), tu_count=len(x_all))
    result.ninja_summary = summarize_compile(
        (tok for args in n_all for tok in args), tu_count=len(n_all))

    # Per-target, matched by normalized name.
    xmap = _aggregate_compile_by_name(xcode)
    nmap = _aggregate_compile_by_name(ninja)
    for name in sorted(set(xmap) & set(nmap)):
        x_args, x_count = xmap[name]
        n_args, n_count = nmap[name]
        target_diff = TargetCompilerDiff(
            name=name,
            categories=_diff_categories(_compile_normalized(x_args, xrel),
                                        _compile_normalized(n_args, nrel)),
            xcode_summary=summarize_compile(
                (tok for a in x_args for tok in a), tu_count=x_count),
            ninja_summary=summarize_compile(
                (tok for a in n_args for tok in a), tu_count=n_count))
        if target_diff.any():
            result.per_target.append(target_diff)
    return result


def diff_linkers(xcode: BuildGraph, ninja: BuildGraph, identities: Iterable[str],
                 xrel: Relativizer, nrel: Relativizer) -> list[LinkerDiff]:
    xtargets = xcode.targets_by_product()
    ntargets = ninja.targets_by_product()
    result: list[LinkerDiff] = []
    for identity in identities:
        xt = xtargets.get(identity)
        nt = ntargets.get(identity)
        if not (xt and nt and xt.link_step and nt.link_step):
            continue
        categories = _diff_categories(
            _strip_target_equivalence(normalize_args(xt.link_step.args, xrel)),
            _strip_target_equivalence(normalize_args(nt.link_step.args, nrel)))
        if categories:
            result.append(LinkerDiff(identity=identity, categories=categories))
    return result


def diff_symbols(xcode: BuildGraph, ninja: BuildGraph, identities: Iterable[str],
                 *, arch: str) -> list[SymbolDiff]:
    xprods = xcode.products()
    nprods = ninja.products()
    result: list[SymbolDiff] = []
    for identity in identities:
        xp, np = xprods.get(identity), nprods.get(identity)
        if not (xp and np and xp.kind in artifacts.INSPECTABLE_KINDS):
            continue
        diff = SymbolDiff(identity=identity)
        xbin = artifacts.resolve_binary(xp)
        nbin = artifacts.resolve_binary(np)
        if not xbin or not nbin:
            missing = 'Xcode' if not xbin else 'Ninja'
            diff.error = f'{missing} binary not found on disk'
            result.append(diff)
            continue
        try:
            xsyms = artifacts.exported_symbols(xbin, arch=arch)
            nsyms = artifacts.exported_symbols(nbin, arch=arch)
        except Exception as error:  # noqa: BLE001 - surface any tool failure
            diff.error = str(error)
            result.append(diff)
            continue
        diff.xcode_count = len(xsyms)
        diff.ninja_count = len(nsyms)
        diff.common_count = len(xsyms & nsyms)
        diff.only_xcode = xsyms - nsyms
        diff.only_ninja = nsyms - xsyms
        result.append(diff)
    return result


def diff_codesign(xcode: BuildGraph, ninja: BuildGraph,
                  identities: Iterable[str]) -> list[CodesignDiff]:
    xprods = xcode.products()
    nprods = ninja.products()
    result: list[CodesignDiff] = []
    for identity in identities:
        xp, np = xprods.get(identity), nprods.get(identity)
        if not (xp and np and xp.kind in artifacts.INSPECTABLE_KINDS):
            continue
        diff = CodesignDiff(identity=identity)
        xbin = artifacts.resolve_binary(xp)
        nbin = artifacts.resolve_binary(np)
        if not xbin or not nbin:
            missing = 'Xcode' if not xbin else 'Ninja'
            diff.error = f'{missing} binary not found on disk'
            result.append(diff)
            continue
        diff.xcode = artifacts.codesign_info(xbin)
        diff.ninja = artifacts.codesign_info(nbin)
        xent = diff.xcode.entitlements
        nent = diff.ninja.entitlements
        diff.entitlements_only_xcode = set(xent) - set(nent)
        diff.entitlements_only_ninja = set(nent) - set(xent)
        diff.entitlement_value_differs = {
            key for key in set(xent) & set(nent) if xent[key] != nent[key]}
        result.append(diff)
    return result


def compare(xcode: BuildGraph, ninja: BuildGraph, *, arch: str,
            repo_root: Optional[str], dimensions: Iterable[str],
            only_identities: Optional[set[str]] = None) -> DiffResult:
    """Run the selected dimensions and assemble a :class:`DiffResult`."""
    from pathlib import Path
    dims = list(dimensions)
    result = DiffResult(arch=arch, xcode_root=str(xcode.root),
                        ninja_root=str(ninja.root), dimensions=dims)

    repo = Path(repo_root) if repo_root else None
    xrel = Relativizer.create(repo_root=repo, build_root=xcode.root)
    nrel = Relativizer.create(repo_root=repo, build_root=ninja.root)

    binaries = diff_binaries(xcode, ninja)
    if 'targets' in dims:
        result.binaries = binaries

    # Products present in both builds are the join set for per-product diffs.
    common = set(binaries.common) | {
        x.identity for x, _ in binaries.kind_mismatch}
    if only_identities is not None:
        common &= only_identities
    common_sorted = sorted(common)

    if 'args' in dims:
        result.compiler = diff_compiler(xcode, ninja, xrel, nrel)
        result.linkers = diff_linkers(xcode, ninja, common_sorted, xrel, nrel)
    if 'symbols' in dims:
        result.symbols = diff_symbols(xcode, ninja, common_sorted, arch=arch)
    if 'codesign' in dims:
        result.codesign = diff_codesign(xcode, ninja, common_sorted)
    return result
