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

import contextlib
import tempfile
from io import StringIO
from pathlib import Path
from unittest import TestCase
from unittest.mock import patch

from webkitbuilddiff import program
from webkitbuilddiff.model import BuildGraph, BuildSystem


class ResolveDimensionsTest(TestCase):
    def test_valid_subset(self):
        self.assertEqual(program._resolve_dimensions('targets,symbols'),
                         ['targets', 'symbols'])

    def test_default_is_all(self):
        parser = program.get_parser()
        options = parser.parse_args([])
        self.assertEqual(program._resolve_dimensions(options.only),
                         ['targets', 'args', 'symbols', 'codesign'])
        self.assertEqual(options.arch, 'arm64e')

    def test_unknown_dimension_raises(self):
        with self.assertRaises(ValueError):
            program._resolve_dimensions('targets,bogus')


class MainTest(TestCase):
    def _make_repo(self) -> Path:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        repo = Path(tmp.name)
        (repo / 'WebKitBuild' / 'Debug').mkdir(parents=True)
        xcbuild = repo / 'WebKitBuild' / 'XCBuildData' / 'abc.xcbuilddata'
        xcbuild.mkdir(parents=True)
        (xcbuild / 'manifest.json').write_text('{}')
        ninja_dir = repo / 'WebKitBuild' / 'cmake-mac' / 'Debug'
        ninja_dir.mkdir(parents=True)
        (ninja_dir / 'build.ninja').write_text('')
        return repo

    def _run(self, argv):
        with (contextlib.redirect_stdout(StringIO()) as out,
              contextlib.redirect_stderr(StringIO())):
            code = program.main(argv)
        return code, out.getvalue()

    def test_missing_manifest_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, _ = self._run(['--repo-root', tmp, '--only', 'targets'])
        self.assertEqual(code, 2)

    def test_unknown_dimension_errors(self):
        repo = self._make_repo()
        code, _ = self._run(['--repo-root', str(repo), '--only', 'bogus'])
        self.assertEqual(code, 2)

    def test_happy_path_no_differences(self):
        repo = self._make_repo()
        empty_x = BuildGraph(build_system=BuildSystem.XCODE, root=repo)
        empty_n = BuildGraph(build_system=BuildSystem.NINJA, root=repo)
        with (patch('webkitbuilddiff.program.xcode.load', return_value=empty_x),
              patch('webkitbuilddiff.program.ninja.load', return_value=empty_n)):
            code, out = self._run(['--repo-root', str(repo), '--only', 'targets'])
        self.assertEqual(code, 0)
        self.assertIn('Comparing Xcode vs Ninja builds', out)

    def test_json_output(self):
        repo = self._make_repo()
        empty_x = BuildGraph(build_system=BuildSystem.XCODE, root=repo)
        empty_n = BuildGraph(build_system=BuildSystem.NINJA, root=repo)
        with (patch('webkitbuilddiff.program.xcode.load', return_value=empty_x),
              patch('webkitbuilddiff.program.ninja.load', return_value=empty_n)):
            code, out = self._run(
                ['--repo-root', str(repo), '--only', 'targets', '--json'])
        import json
        payload = json.loads(out)
        self.assertEqual(payload['arch'], 'arm64e')

    def test_html_out_file_written(self):
        repo = self._make_repo()
        empty_x = BuildGraph(build_system=BuildSystem.XCODE, root=repo)
        empty_n = BuildGraph(build_system=BuildSystem.NINJA, root=repo)
        html_out = repo / 'diff.html'
        with (patch('webkitbuilddiff.program.xcode.load', return_value=empty_x),
              patch('webkitbuilddiff.program.ninja.load', return_value=empty_n)):
            code, out = self._run(
                ['--repo-root', str(repo), '--only', 'targets',
                 '--html-out', str(html_out)])
        self.assertEqual(code, 0)
        # A file-only request keeps stdout quiet.
        self.assertEqual(out, '')
        self.assertTrue(html_out.read_text().startswith('<!DOCTYPE html>'))
