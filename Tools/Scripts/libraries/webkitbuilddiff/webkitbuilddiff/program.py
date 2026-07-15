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
"""CLI front-end for comparing WebKit's Xcode and Ninja builds."""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Optional, Sequence

from webkitbuilddiff import diff, ninja, report, xcode
from webkitbuilddiff.diff import DIMENSIONS

# Exit codes.
_NO_DIFFERENCES = 0
_DIFFERENCES = 1
_ERROR = 2


class Options(argparse.Namespace):
    repo_root: Optional[Path]
    xcode_build: Optional[Path]
    ninja_build: Optional[Path]
    manifest: Optional[Path]
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
        description='Detect performance-impacting differences between WebKit\'s '
                    'Xcode and Ninja builds (targets, compiler/linker flags, '
                    'exported symbols, and codesigning).')
    parser.add_argument(
        '--repo-root', type=Path,
        help='WebKit checkout root (default: current git top-level).')
    parser.add_argument(
        '--xcode-build', type=Path,
        help='Xcode build products dir (default: <repo>/WebKitBuild/Debug).')
    parser.add_argument(
        '--ninja-build', type=Path,
        help='Ninja build dir (default: <repo>/WebKitBuild/cmake-mac/Debug).')
    parser.add_argument(
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


def main(argv: Optional[Sequence[str]] = None) -> int:
    options = get_parser().parse_args(argv, namespace=Options())

    try:
        dimensions = _resolve_dimensions(options.only)
    except ValueError as error:
        print(f'compare-builds: error: {error}', file=sys.stderr)
        return _ERROR

    repo_root = options.repo_root or _git_toplevel() or Path.cwd()
    xcode_build = options.xcode_build or repo_root / 'WebKitBuild' / 'Debug'
    ninja_build = (options.ninja_build or
                   repo_root / 'WebKitBuild' / 'cmake-mac' / 'Debug')
    manifest = options.manifest or xcode.find_manifest(
        xcode_build.parent / 'XCBuildData')

    if manifest is None or not manifest.exists():
        print(f'compare-builds: error: no Xcode manifest found (looked under '
              f'{xcode_build.parent / "XCBuildData"}); pass --manifest.',
              file=sys.stderr)
        return _ERROR
    if not (ninja_build / 'build.ninja').exists():
        print(f'compare-builds: error: no build.ninja under {ninja_build}; '
              f'pass --ninja-build.', file=sys.stderr)
        return _ERROR

    print(f'Loading Xcode graph from {manifest} ...', file=sys.stderr)
    xcode_graph = xcode.load(manifest, build_root=xcode_build)
    print(f'Loading Ninja graph from {ninja_build} ...', file=sys.stderr)
    ninja_graph = ninja.load(ninja_build)

    only_identities = set(options.target) if options.target else None
    result = diff.compare(
        xcode_graph, ninja_graph, arch=options.arch, repo_root=str(repo_root),
        dimensions=dimensions, only_identities=only_identities)

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
