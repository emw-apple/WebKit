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
"""CLI front-end for comparing two WebKit builds.

Two modes:

* **Default** (no ``--a``/``--b``): compare the Xcode build against the
  CMake/Ninja build, auto-discovering both under ``WebKitBuild``.
* **Generic** (``--a DIR --b DIR``): compare any two build directories,
  detecting each one's build system. Useful for comparing two CMake/Ninja
  configurations to validate a large CMake change.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Optional, Sequence

from webkitbuilddiff import diff, ninja, report, xcode
from webkitbuilddiff.diff import DIMENSIONS
from webkitbuilddiff.model import BuildGraph

# Exit codes.
_NO_DIFFERENCES = 0
_DIFFERENCES = 1
_ERROR = 2


class Options(argparse.Namespace):
    repo_root: Optional[Path]
    xcode_build: Optional[Path]
    ninja_build: Optional[Path]
    manifest: Optional[Path]
    a: Optional[Path]
    b: Optional[Path]
    label_a: Optional[str]
    label_b: Optional[str]
    arch: str
    only: str
    target: list[str]
    json: bool
    json_out: Optional[Path]
    html: bool
    html_out: Optional[Path]
    verbose: bool


def _git_toplevel() -> Optional[Path]:
    try:
        result = subprocess.run(
            ('git', 'rev-parse', '--show-toplevel'),
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    except OSError:
        return None
    if result.returncode != 0:
        return None
    return Path(result.stdout.strip())


def get_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog='compare-builds',
        description='Detect performance-impacting differences between two '
                    'WebKit builds (targets, compiler/linker flags, exported '
                    'symbols, and codesigning). By default compares the Xcode '
                    'and Ninja builds; pass --a/--b to compare any two builds '
                    '(e.g. two CMake configurations).')
    generic = parser.add_argument_group(
        'generic mode', 'Compare any two build directories, detecting each '
                        'build system (Ninja if build.ninja is present, else '
                        'Xcode via the sibling XCBuildData manifest).')
    generic.add_argument(
        '--a', type=Path, metavar='DIR',
        help='First build directory (labeled "-").')
    generic.add_argument(
        '--b', type=Path, metavar='DIR',
        help='Second build directory (labeled "+").')
    generic.add_argument(
        '--label-a', metavar='NAME',
        help='Display name for the --a build (default: A).')
    generic.add_argument(
        '--label-b', metavar='NAME',
        help='Display name for the --b build (default: B).')

    legacy = parser.add_argument_group(
        'default mode', 'Xcode vs Ninja, auto-discovered under WebKitBuild.')
    legacy.add_argument(
        '--repo-root', type=Path,
        help='WebKit checkout root (default: current git top-level).')
    legacy.add_argument(
        '--xcode-build', type=Path,
        help='Xcode build products dir (default: <repo>/WebKitBuild/Debug).')
    legacy.add_argument(
        '--ninja-build', type=Path,
        help='Ninja build dir (default: <repo>/WebKitBuild/cmake-mac/Debug).')
    legacy.add_argument(
        '--manifest', type=Path,
        help='Xcode manifest.json (default: newest under the Xcode build\'s '
             'sibling XCBuildData directory).')

    parser.add_argument(
        '--arch', default='arm64e',
        help='Architecture to inspect for symbols (default: arm64e).')
    parser.add_argument(
        '--only', default=','.join(DIMENSIONS),
        help=f'Comma-separated dimensions to compare. Choices: '
             f'{", ".join(DIMENSIONS)} (default: all).')
    parser.add_argument(
        '--target', action='append', default=[], metavar='IDENTITY',
        help='Restrict per-product diffs to this product identity (e.g. '
             'JavaScriptCore.framework). Repeatable.')
    parser.add_argument(
        '--json', action='store_true', help='Emit the diff as JSON to stdout.')
    parser.add_argument(
        '--json-out', type=Path, help='Write the diff as JSON to this file.')
    parser.add_argument(
        '--html', action='store_true',
        help='Emit a self-contained HTML report to stdout.')
    parser.add_argument(
        '--html-out', type=Path,
        help='Write a self-contained HTML report to this file.')
    parser.add_argument(
        '-v', '--verbose', action='store_true',
        help='List every differing item instead of a capped preview.')
    return parser


def _resolve_dimensions(only: str) -> list[str]:
    requested = [d.strip() for d in only.split(',') if d.strip()]
    unknown = [d for d in requested if d not in DIMENSIONS]
    if unknown:
        raise ValueError(f'unknown dimension(s): {", ".join(unknown)}; '
                         f'choose from {", ".join(DIMENSIONS)}')
    return requested


def _load_build(path: Path, *, manifest: Optional[Path] = None) -> BuildGraph:
    """Load a :class:`BuildGraph` from a build directory, detecting its system.

    Ninja if ``build.ninja`` is present; otherwise Xcode via the newest
    ``manifest.json`` under the sibling ``XCBuildData`` directory (or
    ``manifest`` if given). Raises :class:`FileNotFoundError` if neither is
    found.
    """
    if (path / 'build.ninja').exists():
        return ninja.load(path)
    manifest = manifest or xcode.find_manifest(path.parent / 'XCBuildData')
    if manifest and manifest.exists():
        return xcode.load(manifest, build_root=path)
    raise FileNotFoundError(
        f'{path}: not a recognized build directory (no build.ninja, and no '
        f'Xcode manifest.json under {path.parent / "XCBuildData"})')


def _resolve_builds(options: Options
                    ) -> tuple[BuildGraph, BuildGraph, str, str]:
    """Resolve the two builds to compare, returning ``(a, b, a_label, b_label)``.

    Raises :class:`ValueError` (usage) or :class:`FileNotFoundError` (missing
    inputs) on failure; the caller maps these to the error exit code.
    """
    repo_root = options.repo_root or _git_toplevel() or Path.cwd()

    if options.a or options.b:
        if not (options.a and options.b):
            raise ValueError('generic mode requires both --a and --b')
        # Resolve to absolute paths so the relativizer can label each build's
        # own root as $BUILD (and thus cancel it out); otherwise the differing
        # build-directory names leak into every flag as spurious differences.
        a_graph = _load_build(options.a.resolve())
        b_graph = _load_build(options.b.resolve())
        return (a_graph, b_graph,
                options.label_a or 'A', options.label_b or 'B')

    # Default: Xcode vs Ninja, auto-discovered.
    xcode_build = (options.xcode_build or
                   repo_root / 'WebKitBuild' / 'Debug').resolve()
    ninja_build = (options.ninja_build or
                   repo_root / 'WebKitBuild' / 'cmake-mac' / 'Debug').resolve()
    manifest = options.manifest or xcode.find_manifest(
        xcode_build.parent / 'XCBuildData')

    if manifest is None or not manifest.exists():
        raise FileNotFoundError(
            f'no Xcode manifest found (looked under '
            f'{xcode_build.parent / "XCBuildData"}); pass --manifest.')
    if not (ninja_build / 'build.ninja').exists():
        raise FileNotFoundError(
            f'no build.ninja under {ninja_build}; pass --ninja-build.')

    print(f'Loading Xcode graph from {manifest} ...', file=sys.stderr)
    a_graph = xcode.load(manifest, build_root=xcode_build)
    print(f'Loading Ninja graph from {ninja_build} ...', file=sys.stderr)
    b_graph = ninja.load(ninja_build)
    return a_graph, b_graph, 'Xcode', 'Ninja'


def main(argv: Optional[Sequence[str]] = None) -> int:
    options = get_parser().parse_args(argv, namespace=Options())

    try:
        dimensions = _resolve_dimensions(options.only)
    except ValueError as error:
        print(f'compare-builds: error: {error}', file=sys.stderr)
        return _ERROR

    repo_root = options.repo_root or _git_toplevel() or Path.cwd()

    try:
        a_graph, b_graph, a_label, b_label = _resolve_builds(options)
    except (ValueError, FileNotFoundError) as error:
        print(f'compare-builds: error: {error}', file=sys.stderr)
        return _ERROR

    if options.a or options.b:
        print(f'Loaded {a_label} ({a_graph.build_system}) from {a_graph.root}',
              file=sys.stderr)
        print(f'Loaded {b_label} ({b_graph.build_system}) from {b_graph.root}',
              file=sys.stderr)

    only_identities = set(options.target) if options.target else None
    result = diff.compare(
        a_graph, b_graph, arch=options.arch, repo_root=str(repo_root),
        dimensions=dimensions, only_identities=only_identities,
        a_label=a_label, b_label=b_label)

    # Write any requested output files.
    if options.json_out:
        options.json_out.write_text(report.render_json(result) + '\n')
    if options.html_out:
        options.html_out.write_text(
            report.render_html(result, verbose=options.verbose))

    # Choose what goes to stdout: an explicit stdout format wins; a file-only
    # request stays quiet; otherwise the human-readable text report.
    if options.json:
        print(report.render_json(result))
    elif options.html:
        sys.stdout.write(report.render_html(result, verbose=options.verbose))
    elif not (options.json_out or options.html_out):
        sys.stdout.write(report.render_text(result, verbose=options.verbose))

    return _DIFFERENCES if result.has_differences() else _NO_DIFFERENCES


if __name__ == '__main__':
    sys.exit(main())
