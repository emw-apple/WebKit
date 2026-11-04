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

The two sides are called **A** and **B** throughout, and displayed with the
human labels carried on :class:`DiffResult` (``a_label``/``b_label``). By
convention ``-`` marks something present only in A and ``+`` only in B. The
engine is build-system-agnostic: A and B may be an Xcode and a Ninja build
(the default) *or* two builds from the same system (e.g. two CMake/Ninja
configurations), which is useful for validating a large CMake change.

Everything joins on :attr:`Product.identity` (the leaf product name), never on
target names, because the two build systems name and factor targets differently
(Xcode compiles JSC into an intermediate ``libJavaScriptCore.a``; Ninja links
the objects straight into ``JavaScriptCore.framework``). The compiler-flag diff
is therefore reported globally (all TUs) plus best-effort per-target, while
linker flags, symbols, and codesigning join cleanly per product.

Two adjustments apply only when A and B come from **different** build systems,
where the same intent is spelled differently:

* SDK/arch/target flags (``-target arm64e-apple-macos26.6`` vs ``-arch
  arm64e``) are dropped, since they would otherwise read as a difference in
  every product. When both sides are the same system these are compared
  normally, so a genuine SDK or deployment-target change is surfaced.
* Xcode's ``manifest.json`` does not record Swift frontend arguments, so a
  Swift comparison against an Xcode build is one-sided and annotated with a
  note. Same-system Swift comparisons are symmetric.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional

from webkitbuilddiff import artifacts
from webkitbuilddiff.argnorm import (
    CATEGORY_ORDER, NormalizedArgs, Relativizer, Summary, normalize_args,
    summarize_compile, summarize_swift,
)
from webkitbuilddiff.model import (
    BuildGraph, BuildSystem, CodesignInfo, Language, Product,
)

# Dimensions the tool can compare.
DIMENSIONS = ('targets', 'args', 'symbols', 'codesign')

# Compile/link flag tokens excluded from diffing when comparing two *different*
# build systems: they are equal by construction (same SDK/arch) but spelled
# asymmetrically. Compared normally when both sides are the same system.
_COMPILE_IGNORED_PREFIXES = ('-arch', '-target', '-mmacosx-version-min',
                             '-mtargetos', '-isysroot')


@dataclass
class ArgCategoryDiff:
    category: str
    only_a: set[str] = field(default_factory=set)
    only_b: set[str] = field(default_factory=set)

    def any(self) -> bool:
        return bool(self.only_a or self.only_b)


@dataclass
class BinaryDiff:
    only_a: list[Product] = field(default_factory=list)
    only_b: list[Product] = field(default_factory=list)
    kind_mismatch: list[tuple[Product, Product]] = field(default_factory=list)
    common: list[str] = field(default_factory=list)


@dataclass
class TargetCompilerDiff:
    name: str
    categories: list[ArgCategoryDiff] = field(default_factory=list)
    a_summary: Optional[Summary] = None
    b_summary: Optional[Summary] = None

    def any(self) -> bool:
        return (bool(self.categories) or
                self.a_summary != self.b_summary)


@dataclass
class LanguageCompilerDiff:
    """Compiler-flag comparison for a single language (clang or Swift)."""
    language: str
    global_categories: list[ArgCategoryDiff] = field(default_factory=list)
    a_summary: Optional[Summary] = None
    b_summary: Optional[Summary] = None
    per_target: list[TargetCompilerDiff] = field(default_factory=list)
    note: Optional[str] = None

    def any(self) -> bool:
        return (bool(self.global_categories) or
                any(t.any() for t in self.per_target) or
                self.a_summary != self.b_summary)


@dataclass
class CompilerDiff:
    """Compiler-flag comparison, kept separate per language since clang and
    Swift share flag spellings with different semantics."""
    clang: Optional[LanguageCompilerDiff] = None
    swift: Optional[LanguageCompilerDiff] = None

    def languages(self) -> list[LanguageCompilerDiff]:
        return [d for d in (self.clang, self.swift) if d is not None]


@dataclass
class LinkerDiff:
    identity: str
    categories: list[ArgCategoryDiff] = field(default_factory=list)


@dataclass
class SymbolDiff:
    identity: str
    a_count: int = 0
    b_count: int = 0
    common_count: int = 0
    only_a: set[str] = field(default_factory=set)
    only_b: set[str] = field(default_factory=set)
    error: Optional[str] = None


@dataclass
class CodesignDiff:
    identity: str
    a: Optional[CodesignInfo] = None
    b: Optional[CodesignInfo] = None
    entitlements_only_a: set[str] = field(default_factory=set)
    entitlements_only_b: set[str] = field(default_factory=set)
    entitlement_value_differs: set[str] = field(default_factory=set)
    error: Optional[str] = None

    def identifier_differs(self) -> bool:
        return bool(self.a and self.b and
                    self.a.identifier != self.b.identifier)

    def authority_differs(self) -> bool:
        return bool(self.a and self.b and
                    self.a.authority != self.b.authority)

    def flags_differ(self) -> bool:
        return bool(self.a and self.b and self.a.flags != self.b.flags)

    def any(self) -> bool:
        return bool(self.error or self.identifier_differs() or
                    self.authority_differs() or self.flags_differ() or
                    self.entitlements_only_a or
                    self.entitlements_only_b or
                    self.entitlement_value_differs)


@dataclass
class DiffResult:
    arch: str
    a_root: str
    b_root: str
    dimensions: list[str]
    a_label: str = 'A'
    b_label: str = 'B'
    binaries: Optional[BinaryDiff] = None
    compiler: Optional[CompilerDiff] = None
    linkers: list[LinkerDiff] = field(default_factory=list)
    symbols: list[SymbolDiff] = field(default_factory=list)
    codesign: list[CodesignDiff] = field(default_factory=list)

    def has_differences(self) -> bool:
        b = self.binaries
        if b and (b.only_a or b.only_b or b.kind_mismatch):
            return True
        if self.compiler and any(d.any() for d in self.compiler.languages()):
            return True
        if self.linkers or any(s.only_a or s.only_b or s.error
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


def _diff_categories(a: NormalizedArgs,
                     b: NormalizedArgs) -> list[ArgCategoryDiff]:
    diffs: list[ArgCategoryDiff] = []
    categories = set(a.by_category) | set(b.by_category)
    ordered = sorted(categories, key=lambda c: (
        CATEGORY_ORDER.index(c) if c in CATEGORY_ORDER else len(CATEGORY_ORDER),
        c))
    for category in ordered:
        av = a.by_category.get(category, set())
        bv = b.by_category.get(category, set())
        diff = ArgCategoryDiff(category=category,
                               only_a=av - bv, only_b=bv - av)
        if diff.any():
            diffs.append(diff)
    return diffs


def _strip_target_equivalence(normalized: NormalizedArgs) -> NormalizedArgs:
    """Drop SDK/arch/target flags that are equal by construction across builds.

    Both builds target the same arch and SDK, but different build systems spell
    it differently (``-target arm64e-apple-macos26.6`` vs ``-arch arm64e``),
    which would otherwise read as a difference in every product. Only applied
    when comparing across build systems.
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
                        relativizer: Relativizer,
                        language: Language,
                        cross_system: bool) -> NormalizedArgs:
    """Normalize compile flags.

    When ``cross_system`` is set, drop SDK/arch tokens the two build systems
    spell differently but that mean the same thing.
    """
    merged = NormalizedArgs()
    for args in args_iter:
        merged.merge(normalize_args(args, relativizer, language))
    return _strip_target_equivalence(merged) if cross_system else merged


def diff_binaries(a: BuildGraph, b: BuildGraph) -> BinaryDiff:
    aprods = a.products()
    bprods = b.products()
    result = BinaryDiff()
    for identity in sorted(set(aprods) | set(bprods)):
        ap = aprods.get(identity)
        bp = bprods.get(identity)
        if ap and not bp:
            result.only_a.append(ap)
        elif bp and not ap:
            result.only_b.append(bp)
        elif ap and bp:
            if ap.kind != bp.kind:
                result.kind_mismatch.append((ap, bp))
            else:
                result.common.append(identity)
    return result


def _aggregate_compile_by_name(graph: BuildGraph, language: Language
                               ) -> dict[str, tuple[list[list[str]], int]]:
    """Map normalized target name -> (list of TU arg-lists, TU count).

    Only translation units of ``language`` are included.
    """
    out: dict[str, tuple[list[list[str]], int]] = {}
    for target in graph.targets.values():
        steps = [s for s in target.compile_steps if s.language == language]
        if not steps:
            continue
        key = normalize_target_name(target.name)
        arg_lists, count = out.get(key, ([], 0))
        arg_lists = arg_lists + [step.args for step in steps]
        out[key] = (arg_lists, count + len(steps))
    return out


def diff_compiler(a: BuildGraph, b: BuildGraph,
                  arel: Relativizer, brel: Relativizer, *,
                  a_label: str, b_label: str,
                  cross_system: bool) -> CompilerDiff:
    return CompilerDiff(
        clang=_diff_language(a, b, arel, brel, Language.CLANG,
                             a_label=a_label, b_label=b_label,
                             cross_system=cross_system),
        swift=_diff_language(a, b, arel, brel, Language.SWIFT,
                             a_label=a_label, b_label=b_label,
                             cross_system=cross_system))


def _swift_manifest_note(a: BuildGraph, b: BuildGraph, a_all: list,
                         b_all: list, a_label: str,
                         b_label: str) -> Optional[str]:
    """Explain a one-sided Swift comparison caused by an Xcode build.

    Xcode's ``manifest.json`` does not record Swift frontend arguments (they
    live in the execution store), so when one side is Xcode with no Swift steps
    and the other has them, the comparison is expected to be one-sided.
    """
    template = ("{missing}'s Swift compiler arguments are not recorded in "
                "manifest.json; only {present}'s Swift flags are shown.")
    if not a_all and b_all and a.build_system == BuildSystem.XCODE:
        return template.format(missing=a_label, present=b_label)
    if not b_all and a_all and b.build_system == BuildSystem.XCODE:
        return template.format(missing=b_label, present=a_label)
    return None


def _diff_language(a: BuildGraph, b: BuildGraph, arel: Relativizer,
                   brel: Relativizer, language: Language, *,
                   a_label: str, b_label: str, cross_system: bool
                   ) -> Optional[LanguageCompilerDiff]:
    """Compare compiler flags for one language, or None if neither build uses it."""
    def steps(graph: BuildGraph) -> list[list[str]]:
        return [s.args for t in graph.targets.values()
                for s in t.compile_steps if s.language == language]

    summarize = (summarize_swift if language == Language.SWIFT
                 else summarize_compile)
    a_all = steps(a)
    b_all = steps(b)
    if not a_all and not b_all:
        return None

    result = LanguageCompilerDiff(language=language.value)
    result.global_categories = _diff_categories(
        _compile_normalized(a_all, arel, language, cross_system),
        _compile_normalized(b_all, brel, language, cross_system))
    result.a_summary = summarize(
        (tok for args in a_all for tok in args), tu_count=len(a_all)) \
        if a_all else None
    result.b_summary = summarize(
        (tok for args in b_all for tok in args), tu_count=len(b_all)) \
        if b_all else None

    # Per-target, matched by normalized name.
    amap = _aggregate_compile_by_name(a, language)
    bmap = _aggregate_compile_by_name(b, language)
    for name in sorted(set(amap) & set(bmap)):
        a_args, a_count = amap[name]
        b_args, b_count = bmap[name]
        target_diff = TargetCompilerDiff(
            name=name,
            categories=_diff_categories(
                _compile_normalized(a_args, arel, language, cross_system),
                _compile_normalized(b_args, brel, language, cross_system)),
            a_summary=summarize(
                (tok for a_ in a_args for tok in a_), tu_count=a_count),
            b_summary=summarize(
                (tok for b_ in b_args for tok in b_), tu_count=b_count))
        if target_diff.any():
            result.per_target.append(target_diff)

    if language == Language.SWIFT:
        result.note = _swift_manifest_note(a, b, a_all, b_all,
                                           a_label, b_label)
    return result


def diff_linkers(a: BuildGraph, b: BuildGraph, identities: Iterable[str],
                 arel: Relativizer, brel: Relativizer, *,
                 cross_system: bool) -> list[LinkerDiff]:
    atargets = a.targets_by_product()
    btargets = b.targets_by_product()
    result: list[LinkerDiff] = []
    for identity in identities:
        at = atargets.get(identity)
        bt = btargets.get(identity)
        if not (at and bt and at.link_step and bt.link_step):
            continue
        categories = _diff_categories(
            _link_normalized(at.link_step.args, arel, cross_system),
            _link_normalized(bt.link_step.args, brel, cross_system))
        if categories:
            result.append(LinkerDiff(identity=identity, categories=categories))
    return result


def _link_normalized(args: list[str], relativizer: Relativizer,
                     cross_system: bool) -> NormalizedArgs:
    normalized = normalize_args(args, relativizer)
    return _strip_target_equivalence(normalized) if cross_system else normalized


def diff_symbols(a: BuildGraph, b: BuildGraph, identities: Iterable[str],
                 *, arch: str) -> list[SymbolDiff]:
    aprods = a.products()
    bprods = b.products()
    result: list[SymbolDiff] = []
    for identity in identities:
        ap, bp = aprods.get(identity), bprods.get(identity)
        if not (ap and bp and ap.kind in artifacts.INSPECTABLE_KINDS):
            continue
        diff = SymbolDiff(identity=identity)
        abin = artifacts.resolve_binary(ap)
        bbin = artifacts.resolve_binary(bp)
        if not abin or not bbin:
            missing = 'A' if not abin else 'B'
            diff.error = f'{missing} binary not found on disk'
            result.append(diff)
            continue
        try:
            asyms = artifacts.exported_symbols(abin, arch=arch)
            bsyms = artifacts.exported_symbols(bbin, arch=arch)
        except Exception as error:  # noqa: BLE001 - surface any tool failure
            diff.error = str(error)
            result.append(diff)
            continue
        diff.a_count = len(asyms)
        diff.b_count = len(bsyms)
        diff.common_count = len(asyms & bsyms)
        diff.only_a = asyms - bsyms
        diff.only_b = bsyms - asyms
        result.append(diff)
    return result


def diff_codesign(a: BuildGraph, b: BuildGraph,
                  identities: Iterable[str]) -> list[CodesignDiff]:
    aprods = a.products()
    bprods = b.products()
    result: list[CodesignDiff] = []
    for identity in identities:
        ap, bp = aprods.get(identity), bprods.get(identity)
        if not (ap and bp and ap.kind in artifacts.INSPECTABLE_KINDS):
            continue
        diff = CodesignDiff(identity=identity)
        abin = artifacts.resolve_binary(ap)
        bbin = artifacts.resolve_binary(bp)
        if not abin or not bbin:
            missing = 'A' if not abin else 'B'
            diff.error = f'{missing} binary not found on disk'
            result.append(diff)
            continue
        diff.a = artifacts.codesign_info(abin)
        diff.b = artifacts.codesign_info(bbin)
        aent = diff.a.entitlements
        bent = diff.b.entitlements
        diff.entitlements_only_a = set(aent) - set(bent)
        diff.entitlements_only_b = set(bent) - set(aent)
        diff.entitlement_value_differs = {
            key for key in set(aent) & set(bent) if aent[key] != bent[key]}
        result.append(diff)
    return result


def compare(a: BuildGraph, b: BuildGraph, *, arch: str,
            repo_root: Optional[str], dimensions: Iterable[str],
            only_identities: Optional[set[str]] = None,
            a_label: str = 'A', b_label: str = 'B') -> DiffResult:
    """Run the selected dimensions and assemble a :class:`DiffResult`.

    ``a`` and ``b`` are the two builds to compare (``-`` marks A, ``+`` marks
    B); ``a_label``/``b_label`` are how they are named in the report. When the
    two builds are the same build system, SDK/arch equivalence stripping is
    skipped so genuine SDK/arch changes surface.
    """
    dims = list(dimensions)
    cross_system = a.build_system != b.build_system
    result = DiffResult(arch=arch, a_root=str(a.root), b_root=str(b.root),
                        dimensions=dims, a_label=a_label, b_label=b_label)

    repo = Path(repo_root) if repo_root else None
    arel = Relativizer.create(repo_root=repo, build_root=a.root)
    brel = Relativizer.create(repo_root=repo, build_root=b.root)

    binaries = diff_binaries(a, b)
    if 'targets' in dims:
        result.binaries = binaries

    # Products present in both builds are the join set for per-product diffs.
    common = set(binaries.common) | {
        ap.identity for ap, _ in binaries.kind_mismatch}
    if only_identities is not None:
        common &= only_identities
    common_sorted = sorted(common)

    if 'args' in dims:
        result.compiler = diff_compiler(a, b, arel, brel, a_label=a_label,
                                        b_label=b_label,
                                        cross_system=cross_system)
        result.linkers = diff_linkers(a, b, common_sorted, arel, brel,
                                      cross_system=cross_system)
    if 'symbols' in dims:
        result.symbols = diff_symbols(a, b, common_sorted, arch=arch)
    if 'codesign' in dims:
        result.codesign = diff_codesign(a, b, common_sorted)
    return result
