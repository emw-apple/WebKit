# Copyright (C) 2026 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import shutil
import subprocess
import unittest

from unittest.mock import patch

from webkitcorepy import testing
from webkitscmpy import local


def _real_git():
    git = shutil.which('git')
    if not git:
        return None, None
    try:
        output = subprocess.run([git, '--version'], capture_output=True, encoding='utf-8').stdout
        version = tuple(int(part) for part in output.split()[2].split('.')[:2])
    except (OSError, IndexError, ValueError):
        return None, None
    return git, version


GIT, GIT_VERSION = _real_git()


@unittest.skipIf(not GIT or GIT_VERSION < local.Git.MINIMUM_REBUILD_VERSION, 'Requires git {}.{} or later'.format(*local.Git.MINIMUM_REBUILD_VERSION))
class TestGitPlumbing(testing.PathTestCase):
    """Exercises local.Git's commit plumbing against real git, since the behavior under test
    (trees, merges, patch-ids) is exactly what the git mock cannot model faithfully."""
    basepath = 'repository'

    AUTHOR = dict(
        GIT_AUTHOR_NAME='Tim Contributor',
        GIT_AUTHOR_EMAIL='tcontributor@example.com',
        GIT_AUTHOR_DATE='2021-06-01T12:00:00-07:00',
        GIT_COMMITTER_NAME='Tim Committer',
        GIT_COMMITTER_EMAIL='tcommitter@example.com',
        GIT_COMMITTER_DATE='2021-06-02T12:00:00+02:00',
    )

    def setUp(self):
        super().setUp()
        global_config = os.path.join(self.container, 'gitconfig')
        with open(global_config, 'w'):
            pass

        # Isolate from the user's configuration (notably commit signing) and environment
        environment = {key: value for key, value in os.environ.items() if not key.startswith('GIT_')}
        environment.update(GIT_CONFIG_GLOBAL=global_config, GIT_CONFIG_NOSYSTEM='1')
        patcher = patch.dict(os.environ, environment, clear=True)
        patcher.start()
        self.addCleanup(patcher.stop)

        self.git('init', '-q', '-b', 'main')
        self.write('file.txt', 'a\nb\nc\n')
        self.base = self.commit('Base commit\n', 'file.txt')

    def git(self, *args, env=None, input=None, strip=True):
        result = subprocess.run(
            [GIT] + list(args), cwd=self.path, capture_output=True, encoding='utf-8',
            env=dict(os.environ, **(env or {})), input=input,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip() if strip else result.stdout

    def write(self, name, content):
        with open(os.path.join(self.path, name), 'w') as file:
            file.write(content)

    def commit(self, message, *files):
        self.git('add', *files)
        self.git('commit', '-q', '-F', '-', env=self.AUTHOR, input=message)
        return self.git('rev-parse', 'HEAD')

    def files_in(self, ref):
        return self.git('ls-tree', '--name-only', ref).splitlines()

    def test_version(self):
        version = local.Git(self.path).version()
        self.assertGreaterEqual(version, local.Git.MINIMUM_REBUILD_VERSION)

    def test_commit_details(self):
        message = 'Change title\n\nDescription\n\nPull-Request-Branch: eng/change\n'
        self.write('other.txt', 'other\n')
        commit = self.commit(message, 'other.txt')

        details = local.Git(self.path).commit_details(commit)
        self.assertEqual(details['parents'], [self.base])
        self.assertEqual(details['tree'], self.git('rev-parse', '{}^{{tree}}'.format(commit)))
        self.assertEqual(details['message'], message)
        self.assertEqual(details['identity']['GIT_AUTHOR_NAME'], 'Tim Contributor')
        self.assertEqual(details['identity']['GIT_AUTHOR_EMAIL'], 'tcontributor@example.com')
        self.assertEqual(details['identity']['GIT_AUTHOR_DATE'], '2021-06-01T12:00:00-07:00')
        self.assertEqual(details['identity']['GIT_COMMITTER_NAME'], 'Tim Committer')
        self.assertEqual(details['identity']['GIT_COMMITTER_EMAIL'], 'tcommitter@example.com')
        self.assertEqual(details['identity']['GIT_COMMITTER_DATE'], '2021-06-02T12:00:00+02:00')

    def test_commit_details_invalid(self):
        with self.assertRaises(local.Git.Exception):
            local.Git(self.path).commit_details('does-not-exist')

    def test_rebuild_on_parent(self):
        self.write('first.txt', 'first\n')
        first = self.commit('First change\n', 'first.txt')

        repository = local.Git(self.path)
        rebuilt = repository.rebuild_commit(first, self.base, message='Renamed change\n')
        self.assertNotEqual(rebuilt, first)
        self.assertEqual(self.git('rev-parse', '{}^{{tree}}'.format(rebuilt)), self.git('rev-parse', '{}^{{tree}}'.format(first)))
        self.assertEqual(self.git('rev-parse', '{}^'.format(rebuilt)), self.base)
        self.assertEqual(repository.commit_details(rebuilt)['message'], 'Renamed change\n')
        self.assertEqual(repository.commit_details(rebuilt)['identity'], repository.commit_details(first)['identity'])

    def test_rebuild_independent(self):
        self.write('first.txt', 'first\n')
        first = self.commit('First change\n', 'first.txt')
        self.write('second.txt', 'second\n')
        second = self.commit('Second change\n\nWith a description\n', 'second.txt')

        repository = local.Git(self.path)
        rebuilt = repository.rebuild_commit(second, self.base)
        self.assertEqual(self.git('rev-parse', '{}^'.format(rebuilt)), self.base)
        self.assertEqual(self.files_in(rebuilt), ['file.txt', 'second.txt'])
        self.assertEqual(self.git('diff', '--name-only', self.base, rebuilt), 'second.txt')
        self.assertEqual(repository.commit_details(rebuilt)['message'], 'Second change\n\nWith a description\n')
        self.assertEqual(repository.commit_details(rebuilt)['identity'], repository.commit_details(second)['identity'])

        # Neither the branch, index nor working tree are touched
        self.assertEqual(self.git('rev-parse', 'HEAD'), second)
        self.assertEqual(self.git('status', '--porcelain'), '')
        self.assertTrue(os.path.exists(os.path.join(self.path, 'first.txt')))

    def test_rebuild_deterministic(self):
        self.write('first.txt', 'first\n')
        self.commit('First change\n', 'first.txt')
        self.write('second.txt', 'second\n')
        second = self.commit('Second change\n', 'second.txt')

        repository = local.Git(self.path)
        self.assertEqual(repository.rebuild_commit(second, self.base), repository.rebuild_commit(second, self.base))

    def test_rebuild_conflict(self):
        self.write('file.txt', 'a\nB\nc\n')
        self.commit('First change\n', 'file.txt')
        self.write('file.txt', 'a\nBB\nc\n')
        second = self.commit('Depends on the first change\n', 'file.txt')

        with self.assertRaises(local.Git.MergeConflict) as caught:
            local.Git(self.path).rebuild_commit(second, self.base)
        self.assertEqual(caught.exception.files, ['file.txt'])

    def test_rebuild_invalid_base(self):
        self.write('first.txt', 'first\n')
        first = self.commit('First change\n', 'first.txt')

        with self.assertRaises(local.Git.Exception) as caught:
            local.Git(self.path).rebuild_commit(first, 'does-not-exist')
        self.assertNotIsInstance(caught.exception, local.Git.MergeConflict)

    def test_rewrite_messages(self):
        self.git('checkout', '-q', '-b', 'eng/stack')
        commits = []
        for name in ('first', 'second', 'third'):
            self.write('{}.txt'.format(name), name)
            commits.append(self.commit('{} change\n'.format(name.capitalize()), '{}.txt'.format(name)))
        self.write('staged.txt', 'staged\n')
        self.git('add', 'staged.txt')
        self.write('file.txt', 'modified\n')

        repository = local.Git(self.path)
        before = [repository.commit_details(commit) for commit in commits]
        head = repository.rewrite_messages(self.base, {commits[1]: 'Second change\n\nPull-Request-Branch: eng/second\n'})

        self.assertEqual(self.git('rev-parse', 'HEAD'), head)
        self.assertEqual(self.git('symbolic-ref', 'HEAD'), 'refs/heads/eng/stack')
        rewritten = self.git('rev-list', '--reverse', '{}..HEAD'.format(self.base)).splitlines()
        self.assertEqual(rewritten[0], commits[0])
        self.assertNotEqual(rewritten[1], commits[1])
        self.assertNotEqual(rewritten[2], commits[2])

        after = [repository.commit_details(commit) for commit in rewritten]
        self.assertEqual([details['tree'] for details in after], [details['tree'] for details in before])
        self.assertEqual([details['identity'] for details in after], [details['identity'] for details in before])
        self.assertEqual(
            [details['message'] for details in after],
            ['First change\n', 'Second change\n\nPull-Request-Branch: eng/second\n', 'Third change\n'],
        )

        # The index and working tree are untouched
        self.assertEqual(sorted(self.git('status', '--porcelain', strip=False).splitlines()), [' M file.txt', 'A  staged.txt'])

    def test_rewrite_messages_unchanged(self):
        self.git('checkout', '-q', '-b', 'eng/stack')
        self.write('first.txt', 'first\n')
        first = self.commit('First change\n', 'first.txt')

        self.assertEqual(local.Git(self.path).rewrite_messages(self.base, {}), first)
        self.assertEqual(self.git('rev-parse', 'HEAD'), first)

    def test_patch_id(self):
        self.write('first.txt', 'first\n')
        first = self.commit('First change\n', 'first.txt')
        self.write('second.txt', 'second\n')
        second = self.commit('Second change\n', 'second.txt')
        self.git('commit', '-q', '--allow-empty', '-m', 'Empty change', env=self.AUTHOR)
        empty = self.git('rev-parse', 'HEAD')

        repository = local.Git(self.path)
        rebuilt = repository.rebuild_commit(second, self.base, message='Different message\n')
        self.assertIsNotNone(repository.patch_id(second))
        self.assertEqual(repository.patch_id(second), repository.patch_id(rebuilt))
        self.assertNotEqual(repository.patch_id(first), repository.patch_id(second))
        self.assertIsNone(repository.patch_id(empty))
