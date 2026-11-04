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
"""Normalization of compiler and linker command lines.

The two build systems express the same intent with different, noisy spellings:
absolute paths differ, object-file lists are huge, and diagnostic flags vary.
This module reduces an argv to a set of build-system-agnostic, categorized flag
tokens so that :mod:`webkitbuilddiff.diff` surfaces meaningful differences
instead of path churn.
"""
from __future__ import annotations

import os
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional, Protocol

from webkitbuilddiff.model import Language

# A clang optimization level: -O, -O0..-O3, -Os, -Oz, -Ofast, -Og. This is
# matched precisely so language-mode flags like -ObjC++ are not mistaken for it.
_CLANG_OPT_LEVEL = re.compile(r'-O([0-3sz]|fast|g)?$')

# Reporting order for clang flag categories.
CATEGORIES = (
    'defines',
    'includes',
    'sdk',
    'warnings',
    'language',
    'optimization',
    'debug',
    'codegen',
    'linker',
    'misc',
)

# Reporting order for Swift flag categories (Swift's flags do not map onto
# clang's, so it has its own set).
SWIFT_CATEGORIES = (
    'conditionals',
    'search',
    'sdk',
    'language',
    'optimization',
    'debug',
    'features',
    'module',
    'clang-passthrough',
    'frontend',
    'linker',
    'codegen',
    'misc',
)

# Stable ordering across both languages, used when rendering diffs.
CATEGORY_ORDER = CATEGORIES + tuple(
    c for c in SWIFT_CATEGORIES if c not in CATEGORIES)


class Summary(Protocol):
    """Common shape of a per-language compile summary (clang or Swift)."""
    tu_count: int

    def as_dict(self) -> dict[str, object]: ...

# Flags that consume the following argv token as their value.
SEPARATE_VALUE_FLAGS = frozenset({
    '-I', '-isystem', '-iquote', '-iframework', '-F', '-isysroot', '-include',
    '-imacros', '-target', '-arch', '-x', '-o', '-Xlinker', '-Xclang',
    '-filelist', '-framework', '-weak_framework', '-reexport_framework',
    '-allowable_client', '-client_name', '-install_name',
    '-compatibility_version', '-current_version', '-u', '-rpath',
    '-exported_symbols_list', '-unexported_symbols_list', '-order_file',
    '-bundle_loader', '-add_ast_path', '-dependency-info', '-object_path_lto',
    '-MF', '-MT', '-MQ', '-MJ', '-dylib_file',
})

# Separate-value flags whose value + the flag itself are dropped: build-harness
# bookkeeping (output path, dependency files) or object-file lists, none of
# which describe how something is compiled.
DROP_SEPARATE_VALUE_FLAGS = frozenset({
    '-o', '-MF', '-MT', '-MQ', '-MJ', '-dependency-info', '-filelist',
})

# Standalone flags dropped as noise: compile mode toggles, dependency
# generation, and diagnostic/formatting options that never affect output.
DROP_STANDALONE_FLAGS = frozenset({
    '-c', '-S', '-E', '-MD', '-MMD', '-MP', '-MG', '-M', '-MM',
    '-fcolor-diagnostics', '-fno-color-diagnostics', '-fdiagnostics-color',
    '-fansi-escape-codes', '-v', '-###', '-pipe',
})

# Prefixes of standalone flags dropped as diagnostic noise.
DROP_STANDALONE_PREFIXES = (
    '-fdiagnostics-', '-fmessage-length', '-fcolor-diagnostics',
)

# Joined path-bearing prefixes whose embedded path is relativized in place.
_JOINED_PATH_PREFIXES = (
    '-isystem', '-iquote', '-iframework', '-include', '-imacros', '-I', '-L',
    '-F',
)

# --- Swift flag tables (swiftc has different option syntax than clang) ------

# Swift flags that consume the following argv token as their value.
SWIFT_SEPARATE_VALUE_FLAGS = frozenset({
    '-target', '-sdk', '-module-name', '-num-threads', '-j', '-I', '-F',
    '-L', '-Fsystem', '-Xcc', '-Xfrontend', '-Xllvm', '-Xlinker',
    '-swift-version', '-o', '-output-file-map', '-emit-module-path',
    '-emit-objc-header-path', '-emit-module-interface-path',
    '-emit-private-module-interface-path', '-index-store-path',
    '-const-gather-protocols-file', '-explicit-swift-module-map-file',
    '-primary-file', '-serialize-diagnostics-path', '-emit-dependencies-path',
    '-emit-reference-dependencies-path', '-supplementary-output-file-map',
    '-resource-dir', '-vfsoverlay', '-import-objc-header', '-pch-output-dir',
    '-file-compilation-dir', '-coverage-prefix-map', '-debug-prefix-map',
})

# Swift separate-value flags whose flag + value are dropped: output paths and
# per-invocation bookkeeping that do not describe how code is built.
SWIFT_DROP_SEPARATE_VALUE_FLAGS = frozenset({
    '-o', '-output-file-map', '-num-threads', '-j', '-primary-file',
    '-serialize-diagnostics-path', '-emit-dependencies-path',
    '-emit-reference-dependencies-path', '-index-store-path',
    '-supplementary-output-file-map', '-emit-module-path',
    '-emit-objc-header-path', '-emit-module-interface-path',
    '-emit-private-module-interface-path', '-const-gather-protocols-file',
    '-explicit-swift-module-map-file', '-pch-output-dir',
})

# Standalone Swift flags dropped as noise (compile mode, diagnostics).
SWIFT_DROP_STANDALONE_FLAGS = frozenset({
    '-c', '-frontend', '-color-diagnostics', '-no-color-diagnostics',
})

# Swift joined path-bearing prefixes whose embedded path is relativized.
_SWIFT_JOINED_PATH_PREFIXES = ('-I', '-L', '-F', '-Fsystem')


@dataclass
class Relativizer:
    """Rewrites absolute build paths to stable ``$SRC``/``$BUILD``/... labels."""

    # (label, absolute-path-string), most-specific first.
    roots: list[tuple[str, str]] = field(default_factory=list)

    @classmethod
    def create(cls, *, repo_root: Optional[Path] = None,
               build_root: Optional[Path] = None,
               sdk_root: Optional[Path] = None,
               toolchain_root: Optional[Path] = None) -> 'Relativizer':
        pairs: list[tuple[str, str]] = []
        for label, path in (('$SDK', sdk_root), ('$TOOLCHAIN', toolchain_root),
                            ('$BUILD', build_root), ('$SRC', repo_root)):
            if path is not None:
                pairs.append((label, str(path).rstrip(os.sep)))
        # Longest path first: a build root nested under the repo root must win.
        pairs.sort(key=lambda pair: len(pair[1]), reverse=True)
        return cls(roots=pairs)

    def path(self, value: str) -> str:
        for label, root in self.roots:
            if value == root:
                return label
            if value.startswith(root + os.sep):
                return label + value[len(root):]
        return value

    def token(self, tok: str) -> str:
        """Relativize any path embedded in a single argv token."""
        if tok.startswith('-Wl,'):
            return ','.join(self.path(part) for part in tok.split(','))
        for prefix in _JOINED_PATH_PREFIXES:
            if tok.startswith(prefix) and len(tok) > len(prefix):
                rest = tok[len(prefix):]
                # Support both -I/path and -I=/path spellings.
                if rest.startswith('='):
                    return f'{prefix}={self.path(rest[1:])}'
                return prefix + self.path(rest)
        # Generic `--flag=/abs/path` form.
        if '=' in tok:
            head, _, tail = tok.partition('=')
            if tail.startswith(os.sep):
                return f'{head}={self.path(tail)}'
        return tok


@dataclass
class NormalizedArgs:
    """Categorized, path-relativized flags for one or more compile/link steps."""
    by_category: dict[str, set[str]] = field(default_factory=dict)

    def add(self, category: str, token: str) -> None:
        self.by_category.setdefault(category, set()).add(token)

    def all_flags(self) -> set[str]:
        out: set[str] = set()
        for flags in self.by_category.values():
            out |= flags
        return out

    def merge(self, other: 'NormalizedArgs') -> None:
        for category, flags in other.by_category.items():
            self.by_category.setdefault(category, set()).update(flags)


def categorize(token: str) -> str:
    """Bucket a normalized flag token into one of :data:`CATEGORIES`.

    ``startswith`` is case-sensitive, so ``-F`` (framework search path) is
    distinct from ``-framework``/``-f...`` and needs no special guarding.
    """
    if token.startswith(('-D', '-U')):
        return 'defines'
    # Header and framework *search paths* ('-F' is capital; '-framework' is not).
    if token.startswith(('-isystem', '-iquote', '-iframework', '-include',
                          '-imacros', '-I', '-F')):
        return 'includes'
    if token.startswith('-isysroot'):
        return 'sdk'
    if token.startswith('-W') and not token.startswith(('-Wl,', '-Wp,')):
        return 'warnings'
    if token.startswith(('-std', '-stdlib', '-ansi', '-x ')):
        return 'language'
    if _CLANG_OPT_LEVEL.match(token):
        return 'optimization'
    if token.startswith('-g'):
        return 'debug'
    if token.startswith((
            '-Wl,', '-Xlinker', '-l', '-L', '-framework', '-weak_framework',
            '-reexport_framework', '-dynamiclib', '-bundle', '-install_name',
            '-compatibility_version', '-current_version', '-allowable_client',
            '-client_name', '-rpath', '-dead_strip', '-exported_symbols_list',
            '-unexported_symbols_list', '-order_file', '-fobjc-link-runtime',
            '-nostdlib', '-bundle_loader', '-sectcreate', '-headerpad',
            '-object_path_lto', '-fapplication-extension')):
        return 'linker'
    if token.startswith(('-f', '-m', '-arch', '-target')):
        return 'codegen'
    return 'misc'


def categorize_swift(token: str) -> str:
    """Bucket a normalized Swift flag token into a :data:`SWIFT_CATEGORIES` name.

    Swift shares spellings with clang but not meaning: ``-D`` is a conditional
    compilation flag (not a preprocessor macro), ``-O``/``-Onone`` are Swift
    optimization modes, and ``-Xcc`` forwards a flag to the embedded clang.
    """
    if token.startswith('-D'):
        return 'conditionals'
    if token.startswith('-Xcc'):
        return 'clang-passthrough'
    if token.startswith(('-Xfrontend', '-Xllvm')):
        return 'frontend'
    if token.startswith('-Xlinker'):
        return 'linker'
    if token.startswith(('-I', '-L', '-F', '-Fsystem', '-vfsoverlay',
                         '-import-objc-header')):
        return 'search'
    if token.startswith('-sdk'):
        return 'sdk'
    if token.startswith('-swift-version'):
        return 'language'
    if token in ('-O', '-Onone', '-Osize', '-Ounchecked'):
        return 'optimization'
    if token.startswith(('-g', '-debug-info', '-debug-prefix-map')):
        return 'debug'
    if token.startswith(('-enable-', '-disable-')):
        return 'features'
    if token.startswith((
            '-module-name', '-emit-module', '-emit-objc-header',
            '-emit-library', '-parse-as-library', '-wmo',
            '-whole-module-optimization', '-enable-batch-mode', '-static',
            '-application-extension', '-cross-module-optimization')):
        return 'module'
    if token.startswith('-target'):
        return 'codegen'
    return 'misc'


def normalize_args(args: Iterable[str],
                   relativizer: Optional[Relativizer] = None,
                   language: Language = Language.CLANG) -> NormalizedArgs:
    """Reduce a raw argv to categorized, relativized flag tokens.

    Positional inputs (the compiler path, sources, object files, ``@response``
    files) and pure build bookkeeping are dropped so only the flags that
    influence how a target is built remain. ``language`` selects clang or Swift
    option semantics.
    """
    if language == Language.SWIFT:
        return _normalize_swift(args, relativizer or Relativizer())
    return _normalize_clang(args, relativizer or Relativizer())


def _normalize_clang(tokens_iter: Iterable[str],
                     rel: Relativizer) -> NormalizedArgs:
    tokens = list(tokens_iter)
    result = NormalizedArgs()
    i = 0
    n = len(tokens)
    while i < n:
        tok = tokens[i]
        if tok in SEPARATE_VALUE_FLAGS:
            value = tokens[i + 1] if i + 1 < n else ''
            i += 2
            if tok in DROP_SEPARATE_VALUE_FLAGS:
                continue
            normalized = f'{tok} {rel.path(value)}'
            result.add(categorize(normalized), normalized)
            continue
        i += 1
        if tok in DROP_STANDALONE_FLAGS or tok.startswith(DROP_STANDALONE_PREFIXES):
            continue
        # Positional inputs: the compiler executable, source and object files,
        # and @response files. These are not flags.
        if not tok.startswith('-') or tok.startswith('@'):
            continue
        normalized = rel.token(tok)
        result.add(categorize(normalized), normalized)
    return result


def _normalize_swift(tokens_iter: Iterable[str],
                     rel: Relativizer) -> NormalizedArgs:
    tokens = list(tokens_iter)
    result = NormalizedArgs()
    i = 0
    n = len(tokens)
    while i < n:
        tok = tokens[i]
        if tok in SWIFT_SEPARATE_VALUE_FLAGS:
            value = tokens[i + 1] if i + 1 < n else ''
            i += 2
            if tok in SWIFT_DROP_SEPARATE_VALUE_FLAGS:
                continue
            normalized = f'{tok} {rel.path(value)}'
            result.add(categorize_swift(normalized), normalized)
            continue
        i += 1
        if tok in SWIFT_DROP_STANDALONE_FLAGS:
            continue
        if not tok.startswith('-') or tok.startswith('@'):
            continue
        normalized = _relativize_swift_token(tok, rel)
        result.add(categorize_swift(normalized), normalized)
    return result


def _relativize_swift_token(tok: str, rel: Relativizer) -> str:
    for prefix in _SWIFT_JOINED_PATH_PREFIXES:
        if tok.startswith(prefix) and len(tok) > len(prefix):
            return prefix + rel.path(tok[len(prefix):])
    return tok


@dataclass
class CompileSummary:
    """Perf-relevant facts derived from a target's compile flags."""
    optimization: str = '-O0'          # clang's default when unspecified
    debug_info: bool = False
    cxx_std: Optional[str] = None
    stdlib: Optional[str] = None
    lto: str = 'none'                  # none | full | thin
    sanitizers: list[str] = field(default_factory=list)
    modules: bool = False
    tu_count: int = 0

    def as_dict(self) -> dict[str, object]:
        return {
            'optimization': self.optimization,
            'debug_info': self.debug_info,
            'cxx_std': self.cxx_std,
            'stdlib': self.stdlib,
            'lto': self.lto,
            'sanitizers': sorted(self.sanitizers),
            'modules': self.modules,
            'tu_count': self.tu_count,
        }


# Well-known equivalent spellings that should not read as differences.
_OPT_ALIASES = {'-Onone': '-O0'}
_CXX_STD_ALIASES = {
    'c++2a': 'c++20', 'c++2b': 'c++23', 'c++2c': 'c++26',
    'c++1z': 'c++17', 'c++1y': 'c++14', 'c++0x': 'c++11',
    'gnu++2a': 'gnu++20', 'gnu++2b': 'gnu++23', 'gnu++2c': 'gnu++26',
    'gnu++1z': 'gnu++17', 'gnu++1y': 'gnu++14', 'gnu++0x': 'gnu++11',
}


def summarize_compile(all_args: Iterable[str], *, tu_count: int) -> CompileSummary:
    """Derive a :class:`CompileSummary` from the union of a target's raw flags."""
    summary = CompileSummary(tu_count=tu_count)
    sanitizers: set[str] = set()
    for tok in all_args:
        if tok in _OPT_ALIASES:
            summary.optimization = _OPT_ALIASES[tok]
        elif _CLANG_OPT_LEVEL.match(tok):
            summary.optimization = tok
        elif tok == '-g' or (tok.startswith('-g') and tok not in ('-g0',)):
            summary.debug_info = True
        elif tok == '-g0':
            summary.debug_info = False
        elif tok.startswith('-std='):
            std = tok[len('-std='):]
            summary.cxx_std = _CXX_STD_ALIASES.get(std, std)
        elif tok.startswith('-stdlib='):
            summary.stdlib = tok[len('-stdlib='):]
        elif tok == '-flto':
            summary.lto = 'full'
        elif tok.startswith('-flto='):
            summary.lto = tok[len('-flto='):]
        elif tok.startswith('-fsanitize='):
            sanitizers.update(tok[len('-fsanitize='):].split(','))
        elif tok in ('-fmodules', '-fcxx-modules'):
            summary.modules = True
    summary.sanitizers = sorted(sanitizers)
    return summary


@dataclass
class SwiftSummary:
    """Perf-relevant facts derived from a target's Swift compile flags."""
    optimization: str = '-Onone'       # swiftc's default for debug builds
    debug_info: bool = False
    swift_version: Optional[str] = None
    wmo: bool = False                  # whole-module optimization / batch mode
    library_evolution: bool = False
    cross_module_optimization: bool = False
    tu_count: int = 0

    def as_dict(self) -> dict[str, object]:
        return {
            'optimization': self.optimization,
            'debug_info': self.debug_info,
            'swift_version': self.swift_version,
            'wmo': self.wmo,
            'library_evolution': self.library_evolution,
            'cross_module_optimization': self.cross_module_optimization,
            'tu_count': self.tu_count,
        }


def summarize_swift(all_args: Iterable[str], *, tu_count: int) -> SwiftSummary:
    """Derive a :class:`SwiftSummary` from the union of a target's Swift flags."""
    summary = SwiftSummary(tu_count=tu_count)
    tokens = list(all_args)
    for index, tok in enumerate(tokens):
        if tok in ('-O', '-Onone', '-Osize', '-Ounchecked'):
            summary.optimization = tok
        elif tok == '-g':
            summary.debug_info = True
        elif tok == '-gnone':
            summary.debug_info = False
        elif tok == '-swift-version' and index + 1 < len(tokens):
            summary.swift_version = tokens[index + 1]
        elif tok in ('-wmo', '-whole-module-optimization', '-enable-batch-mode'):
            summary.wmo = True
        elif tok == '-enable-library-evolution':
            summary.library_evolution = True
        elif tok == '-cross-module-optimization':
            summary.cross_module_optimization = True
    return summary
