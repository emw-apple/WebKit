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
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional

# Reporting order for flag categories.
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
    if token.startswith('-O'):
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


def normalize_args(args: Iterable[str],
                   relativizer: Optional[Relativizer] = None) -> NormalizedArgs:
    """Reduce a raw argv to categorized, relativized flag tokens.

    Positional inputs (the compiler path, sources, object files, ``@response``
    files) and pure build bookkeeping are dropped so only the flags that
    influence how a target is built remain.
    """
    rel = relativizer or Relativizer()
    tokens = list(args)
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
        if tok.startswith('-O'):
            summary.optimization = _OPT_ALIASES.get(tok, tok)
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
