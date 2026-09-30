# Copyright (C) 2021-2025 Apple Inc. All rights reserved.
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

import argparse
import os
import re
import sys
import time

from .command import Command
from .commit import Commit
from .branch import Branch
from .install_hooks import InstallHooks
from .squash import Squash

from webkitbugspy import Tracker, radar
from webkitcorepy import arguments, run, string_utils, Terminal, OutputCapture
from webkitscmpy import local, log, remote
from webkitscmpy import Commit as CommitModel, PullRequest as PullRequestModel


class PullRequest(Command):
    name = 'pull-request'
    aliases = ['pr', 'pfr', 'upload']
    help = 'Push the current checkout state as a pull-request'
    BLOCKED_LABEL = 'merging-blocked'
    SKIP_EWS_LABEL = 'skip-ews'
    MERGE_LABELS = ['merge-queue']
    UNSAFE_MERGE_LABELS = ['unsafe-merge-queue']
    PER_COMMIT_CONFIG = 'branch.{}.per-commit'

    @classmethod
    def parser(cls, parser, loggers=None):
        Branch.parser(parser, loggers=loggers)
        Squash.parser(parser, loggers=loggers)
        parser.add_argument(
            '--add', '--no-add',
            dest='will_add', default=None,
            help='When drafting a change, add (or never add) modified files to set of staged changes to be committed',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--rebase', '--no-rebase', '--update', '--no-update',
            dest='rebase', default=None,
            help='Rebase (or do not rebase) the pull-request on the source branch before pushing',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--squash', '--no-squash',
            dest='squash', default=None,
            help='Combine all commits on the current development branch into a single commit before pushing',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--defaults', '--no-defaults', action=arguments.NoAction, default=None,
            help='Do not prompt the user for defaults, always use (or do not use) them',
        )
        parser.add_argument(
            '--reopen-closed', '--no-reopen-closed',
            dest='reopen_closed', default=None,
            help='Re-use and re-open (or never re-use) an existing closed pull-request associated with the current branch. '
                 'Without this argument, non-interactive runs always create a new pull-request.',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--overwrite', '--amend', action='store_const', const='overwrite',
            dest='technique', default=None,
            help='When creating a pull request, overwrite the existing commit by default',
        )
        parser.add_argument(
            '--append', action='store_const', const='append',
            dest='technique', default=None,
            help='When creating a pull request, append a new commit on the existing branch by default',
        )
        parser.add_argument(
            '--commit', '--no-commit',
            dest='commit', default=None,
            help='When creating a pull request, create (or do not create) a commit from the set of staged changes',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--with-history', '--no-history',
            dest='history', default=None,
            help='Create numbered branches to track the history of a change',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--set-upstream', '--no-set-upstream',
            dest='set_upstream', default=None,
            help='Set the upstream of the local branch when pushing',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--draft', dest='draft', action='store_true', default=None,
            help='Mark a pull request as a draft when creating it',
        )
        parser.add_argument(
            '--remote', dest='remote', type=str, default=None,
            help='Make a pull request against a specific remote',
        )
        parser.add_argument(
            '--checks', '--no-checks',
            dest='checks', default=None,
            help='Explicitly enable or disable automatic pre-flight checks',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '-o', '--open',
            dest='open', default=None,
            help='Automatically open the PR after creating it.',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--ews', '--skip-ews', '--no-ews',
            dest='ews', default=True,
            help='Enable or disable EWS on the PR',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--update-title', '--no-update-title',
            dest='update_title', default=None,
            help="When updating a pull request, update (or don't update) its title with the commits' common prefix (also configurable via webkitscmpy.update-title).",
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--no-issue', '--no-bug',
            dest='update_issue', default=True,
            help='Disable automatic bug creation and updates',
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--update-radar', '--no-update-radar',
            dest='update_radar', default=True,
            help=('Update the state of the associated Radar when creating a pull request'
                  if radar.Tracker.radarclient() is not None
                  else argparse.SUPPRESS),
            action=arguments.NoAction,
        )
        parser.add_argument(
            '--security', '--redacted',
            dest='redact', action='store_true',
            default=False,
            help='Force the a PR onto a secure remote, regardless of the current branch and issue state.',
        )
        parser.add_argument(
            '--per-commit', '--no-per-commit',
            dest='per_commit', default=None,
            help='Upload each commit on the current branch as its own pull request against the branch it is based on '
                 '(or upload the whole branch as a single pull request). Remembered for the current branch.',
            action=arguments.NoAction,
        )

    @classmethod
    def create_commit(cls, args, repository, **kwargs):
        # First, find the set of files to be modified
        modified = [] if args.will_add is False else repository.modified()
        if args.will_add:
            modified = list(set(modified).union(set(repository.modified(staged=False))))

        # Next, add all modified file
        for file in set(modified) - set(repository.modified(staged=True)):
            log.info('    Adding {}...'.format(file))
            if run([repository.executable(), 'add', file], cwd=repository.root_path).returncode:
                sys.stderr.write("Failed to add '{}'\n".format(file))
                return 1

        # Then, see if we already have a commit associated with this branch we need to modify
        has_commit = repository.commit(include_log=False, include_identifier=False).branch == repository.branch and repository.branch != repository.default_branch
        if not modified and has_commit:
            if not getattr(args, '_bug_urls', None) or os.environ.get('COMMIT_MESSAGE_REVERT'):
                log.info('Using committed changes...')
                return 0

        bug_urls = getattr(args, '_bug_urls', None) or ''
        if isinstance(bug_urls, (list, tuple)):
            bug_urls = '\n'.join(bug_urls)

        # Otherwise, we need to create a commit
        will_amend = has_commit and (args.technique == 'overwrite' or bool(getattr(args, '_bug_urls', None)))
        if not modified and not will_amend:
            sys.stderr.write('No modified files\n')
            return 1
        log.info('Amending commit...' if will_amend else 'Creating commit...')
        env = os.environ
        if getattr(args, '_title', None):
            env['COMMIT_MESSAGE_TITLE'] = getattr(args, '_title')
        if bug_urls:
            env['COMMIT_MESSAGE_BUG'] = bug_urls
        if run(
            [repository.executable(), 'commit', '--date=now'] + (['--amend'] if will_amend else []),
            cwd=repository.root_path,
            env=env,
        ).returncode:
            sys.stderr.write('Failed to generate commit\n')
            return 1

        return 0

    @classmethod
    def title_for(cls, commits):
        title = os.path.commonprefix([commit.message.splitlines()[0] for commit in commits if commit.message])
        if not title:
            title = commits[0].message.splitlines()[0] if commits[0].message else '???'
        title = title.rstrip().lstrip()
        return title[:-5].rstrip() if title.endswith('(Part') else title

    @classmethod
    def issue_from_commits(cls, repository, source_remote, branch_point):
        head = repository.commit(include_log=True, include_identifier=False)
        if run([
            repository.executable(), 'merge-base', '--is-ancestor',
            head.hash, 'remotes/{}/{}'.format(source_remote, branch_point.branch),
        ], capture_output=True, cwd=repository.root_path).returncode:
            if head.issues:
                return head.issues[0].link
            revert_message = os.environ.get('COMMIT_MESSAGE_REVERT', '')
            for line in revert_message.splitlines():
                issue = Tracker.from_string(line)
                if issue:
                    return issue.link
        return None

    @classmethod
    def check_pull_request_args(cls, repository, args):
        if not args.technique:
            args.technique = repository.config()['webkitscmpy.pull-request']
        if args.history is None:
            args.history = dict(
                always=True,
                disabled=False,
                never=False,
            ).get(repository.config()['webkitscmpy.history'])
        if args.history and repository.config()['webkitscmpy.history'] == 'never':
            sys.stderr.write('History retention was requested, but repository configuration forbids it\n')
            return False
        return True

    @classmethod
    def check_redaction_args(cls, repository, args):
        if args.redact and len(repository.source_remotes()) <= 1:
            sys.stderr.write('No secure remotes found in the current checkout\n')
            return False
        if args.redact and repository.source_remotes()[0] == args.remote:
            sys.stderr.write("'{}' is not a secure remote\n".format(args.remote))
            sys.stderr.write("'--remote={}' is incompatible with '--redacted'\n".format(args.remote))
            return False
        return True

    @classmethod
    def source_remote_for(cls, repository, args, branch_point):
        source_remote = args.remote
        if not source_remote:
            bp_remotes = set(repository.branches_for(hash=branch_point.hash, remote=None).keys())
            if len(bp_remotes) == 1:
                # If there is only one remote, that means the branch point doesn't exist on any remote
                # In that case, pick the remote with the most updated version of the branch in question
                remote_head = None
                for remote in repository.source_remotes():
                    try:
                        candidate = repository.find('remotes/{}/{}'.format(remote, branch_point.branch), include_log=False)
                        if not remote_head or candidate.identifier > remote_head.identifier:
                            remote_head = candidate
                            source_remote = remote
                    except ValueError:
                        pass
            else:
                for remote in repository.source_remotes():
                    if remote in bp_remotes:
                        source_remote = remote
                        break
            if source_remote != repository.default_remote:
                print("Making pull request against '{}' because that is where the branch point is from".format(source_remote))
                if branch_point.branch in repository.DEFAULT_BRANCHES:
                    sys.stderr.write('Branch point is on the default branch\n')
                    sys.stderr.write("Local record of '{}' may be out of date\n".format(repository.default_remote))
                    sys.stderr.write("Update with 'git fetch {}'\n".format(repository.default_remote))
            if source_remote and source_remote != repository.default_remote:
                args.remote = source_remote
        if not source_remote:
            source_remote = repository.default_remote
        if args.redact and source_remote == repository.default_remote:
            source_remote = repository.source_remotes()[-1]
            args.remote = source_remote
        return source_remote

    @classmethod
    def match_base_to_remote(cls, repository, source_remote, branch_point):
        if not repository.config().get('remote.{}.url'.format(source_remote)):
            sys.stderr.write("'{}' is not a remote in this repository\n".format(source_remote))
            return False

        did_local_branch_diverge = bool(run([
            repository.executable(), 'merge-base', '--is-ancestor',
            branch_point.branch,
            'remotes/{}/{}'.format(source_remote, branch_point.branch),
        ], cwd=repository.root_path).returncode)
        if did_local_branch_diverge and run([
            repository.executable(), 'branch', '-f',
            branch_point.branch,
            'remotes/{}/{}'.format(source_remote, branch_point.branch),
        ], cwd=repository.root_path).returncode:
            sys.stderr.write("Failed to match '{}' to it's remote '{}'\n".format(branch_point.branch, source_remote))
            return False
        return True

    @classmethod
    def pull_request_branch_point(cls, repository, args, name_prefix=None, **kwargs):
        if not cls.check_redaction_args(repository, args):
            return None

        branch_point = repository.branch_point()
        if not branch_point:
            sys.stderr.write('Failed to determine where pull-request diverged from production branch\n')
            return None
        source_remote = cls.source_remote_for(repository, args, branch_point)

        if not repository.is_suitable_branch_for_pull_request(repository.branch, source_remote):
            if not args.issue:
                args.issue = cls.issue_from_commits(repository, source_remote, branch_point)

            if Branch.main(
                args, repository,
                why="'{}' is not a pull request branch".format(repository.branch),
                redact=source_remote != repository.default_remote,
                target_remote='fork' if source_remote == repository.default_remote else '{}-fork'.format(source_remote),
                name_prefix=name_prefix,
                **kwargs
            ):
                sys.stderr.write("Abandoning pushing pull-request because '{}' could not be created\n".format(args.issue))
                return None

        elif args.issue and repository.branch != args.issue:
            error = "Creating a pull-request for '{}' but we're on '{}'\n".format(args.issue, repository.branch)
            if not repository.dev_branches.match(repository.branch):
                sys.stderr.write(error)
                return None

            if string_utils.decode(args.issue).isnumeric():
                issue = Tracker.instance().issue(int(args.issue))
            else:
                issue = Tracker.from_string(args.issue)
            if not issue:
                sys.stderr.write(error)
                return None
            if not Branch.branch_matches_issue(repository, repository.branch, issue):
                sys.stderr.write(error)
                return None

            if not issue.tracker.hide_title:
                args._title = issue.title
            args._bug_urls = Commit.bug_urls(issue)

        elif not args.issue and getattr(args, 'update_issue', True) and Tracker.instance():
            args.issue = cls.issue_from_commits(repository, source_remote, branch_point)
            if not args.issue:
                issue, result = Branch.ensure_issue(args, repository, redact=source_remote != repository.default_remote)
                if result:
                    return None

                cls.write_branch_variables(
                    repository, repository.branch,
                    title=getattr(args, '_title', None) or '',
                    bug=getattr(args, '_bug_urls', None) or [],
                )

        if not cls.match_base_to_remote(repository, source_remote, branch_point):
            return None
        return branch_point

    @classmethod
    def find_existing_pull_request(cls, repository, remote, branch=None):
        branch = branch or repository.branch
        existing_pr = None
        user, _ = remote.credentials(required=False)
        for pr in remote.pull_requests.find(opened=None, head=branch):
            # GitHub's search apparently uses substring matching, so check for an exact match.
            if branch != pr.head:
                continue
            if existing_pr and existing_pr.opened and not pr.opened:
                continue
            existing_pr = pr
            if not existing_pr.opened:
                continue
            if user and existing_pr.author == user:
                break
        return existing_pr

    @classmethod
    def will_reopen_closed_pull_request(cls, args, repository, existing_pr, branch=None):
        """Decide if a closed pull-request should be re-used (and re-opened) instead of creating a new one.

        Non-interactive invocations never re-use a closed pull-request unless '--reopen-closed' is
        explicitly passed, since a closed pull-request usually means the change it described is no
        longer the change being pushed.
        """
        reopen_closed = getattr(args, 'reopen_closed', None)
        if reopen_closed is not None:
            return reopen_closed
        if args.defaults is not None:
            return False
        return Terminal.choose(
            "'{}' is already associated with '{}', which is closed.\nWould you like to create a new pull-request?".format(branch or repository.branch, existing_pr),
            default='No',
        ) != 'Yes'

    @classmethod
    def pre_pr_checks(cls, repository, add_edits=True):
        num_checks = 0
        log.info('Running pre-PR checks...')
        for key, path in repository.config().items():
            if not key.startswith('webkitscmpy.pre-pr.'):
                continue
            num_checks += 1
            name = key.split('.')[-1]
            log.info('    Running {}...'.format(name))
            while True:
                command_line = path.split(' ')
                if command_line[0] == 'python3' and os.name == 'nt':
                    command_line[0] = sys.executable
                command = run(command_line, cwd=repository.root_path)
                if command.returncode == 0:
                    log.info('    Ran {}!'.format(name))
                    break
                options = ['Yes', 'Retry', 'No']
                response = Terminal.choose(
                    '{} failed!\nRetry will amend the commit with your changes. Continue uploading pull request?'.format(name),
                    options=options,
                    default='No',
                )
                if response == 'No':
                    sys.stderr.write('Pre-PR check {} failed\n'.format(name))
                    return False
                if response == 'Yes':
                    log.info('    {} failed, continuing PR upload anyway'.format(name))
                    break

                modified = [] if add_edits is False else repository.modified()
                if add_edits:
                    modified = list(set(modified).union(set(repository.modified(staged=False))))
                for file in set(modified) - set(repository.modified(staged=True)):
                    log.info('    Adding {}...'.format(file))
                    if run([repository.executable(), 'add', file], cwd=repository.root_path).returncode:
                        sys.stderr.write("Failed to add '{}'\n".format(file))
                        return False

                if modified and run(
                    [repository.executable(), 'commit', '--amend', '--date=now', '--no-edit'],
                    cwd=repository.root_path,
                ).returncode:
                    sys.stderr.write('Pre-PR check {} failed, and commit amend \n'.format(name))
                    return False

        if num_checks:
            log.info('All pre-PR checks run!')
        else:
            log.info('No pre-PR checks to run')
        return True

    @classmethod
    def is_revert_commit(cls, commit):
        if not commit.message:
            return False
        msg = commit.message.split()
        if not len(msg):
            return False
        title = msg[0]
        return title.startswith('Revert')

    @classmethod
    def add_comment_to_reverted_commit_bug_tracker(cls, repository, args, pr, commit):
        source_remote = args.remote or repository.default_remote
        rmt = repository.remote(name=source_remote)
        if not rmt:
            sys.stderr.write("'{}' doesn't have a recognized remote\n".format(repository.root_path))
            return 1
        if not rmt.pull_requests:
            sys.stderr.write("'{}' cannot generate pull-requests\n".format(rmt.url))
            return 1

        log.info('Adding comment for reverted commits...')
        for issue in commit.issues:
            issue.open(why='Reverted by {}'.format(pr.url))
        return 0

    @classmethod
    def add_comment_to_issue(cls, issue, pr, commit_class=None):
        log.info('Checking issue assignee...')
        assigned = False
        if issue.assignee != issue.tracker.me() and commit_class != 'Gardening':
            issue.assign(issue.tracker.me())
            assigned = True
            print('Assigning associated issue to {}'.format(issue.tracker.me()))
        log.info('Checking for pull request link in associated issue...')
        pr_label = 'Test gardening pull request' if commit_class == 'Gardening' else 'Pull request'
        if pr.url and not any([pr.url in comment.content for comment in issue.comments]):
            if issue.opened:
                # Wait until the next second so Bugzilla sends a notification for the PR opening comment
                if assigned:
                    time.sleep(1.1)
                issue.add_comment('{}: {}'.format(pr_label, pr.url))
            elif commit_class != 'Gardening':
                issue.open(why='Re-opening for {} {}'.format(pr_label.lower(), pr.url))
            print('Posted pull request link to {}'.format(issue.link))

    @classmethod
    def will_rebase(cls, repository, args):
        if args.rebase is not None:
            return args.rebase
        return repository.config().get(
            'webkitscmpy.auto-rebase-branch',
            repository.config().get('pull.rebase', 'true'),
        ) == 'true'

    @classmethod
    def rebase_on_source(cls, repository, source_remote, branch_point):
        """Rebase the current branch on the source remote's copy of the branch it's based on,
        returning the new branch point, or None if the rebase failed."""
        log.info("Rebasing '{}' on '{}'...".format(repository.branch, branch_point.branch))
        if repository.pull(rebase=True, branch=branch_point.branch, remote=source_remote):
            sys.stderr.write("Failed to rebase '{}' on '{},' please resolve conflicts\n".format(repository.branch, branch_point.branch))
            return None
        log.info("Rebased '{}' on '{}!'".format(repository.branch, branch_point.branch))
        return repository.commit(branch='{}/{}'.format(source_remote, branch_point.branch))

    @classmethod
    def run_checks(cls, repository, args):
        if args.checks is None:
            args.checks = repository.config().get('webkitscmpy.auto-check', 'false') == 'true'
        if args.checks and not cls.pre_pr_checks(repository, add_edits=not (args.will_add is False)):
            sys.stderr.write('Checks have failed, aborting pull request.\n')
            return False
        return True

    @classmethod
    def split_issues(cls, issues):
        radar_issue = next(iter(filter(lambda issue: isinstance(issue.tracker, radar.Tracker), issues)), None)
        not_radar = next(iter(filter(lambda issue: not isinstance(issue.tracker, radar.Tracker), issues)), None)
        return radar_issue, not_radar

    @classmethod
    def cc_radar(cls, repository, args, issues, update_issue):
        radar_issue, not_radar = cls.split_issues(issues)
        radar_cc_default = repository.config().get('webkitscmpy.cc-radar', 'true') == 'true'
        if update_issue and radar_issue and not_radar and radar_issue.tracker.radarclient() and (args.cc_radar or (radar_cc_default and args.cc_radar is not False)):
            try:
                not_radar.cc_radar(radar=radar_issue)
            except ValueError:
                sys.stderr.write('Aborting pull request.\n')
                return False
        return True

    @classmethod
    def remote_for_issues(cls, repository, args, source_remote, issues):
        redaction_exemption = None
        redacted_issue = None
        for candidate in issues:
            if getattr(candidate.redacted, 'exemption', False):
                redaction_exemption = candidate
            elif candidate.redacted:
                redacted_issue = candidate
        if redaction_exemption:
            print('A commit you are uploading references {}'.format(redaction_exemption.link))
            print("{} {}".format(redaction_exemption.link, redaction_exemption.redacted))
            if redacted_issue:
                sys.stderr.write("Redaction exemption overrides the redaction of {}\n".format(redacted_issue.link))
                sys.stderr.write("{} {}\n".format(redacted_issue.link, redacted_issue.redacted))
            redacted_issue = None

        remote_repo = repository.remote(name=source_remote)
        if isinstance(remote_repo, remote.GitHub) and redacted_issue and args.remote is None:
            print('A commit you are uploading references {}'.format(redacted_issue.link))
            print("{} {}".format(redacted_issue.link, redacted_issue.redacted))
            print("Pull request needs to be sent to a secure remote for review")
            original_remote = source_remote
            if len(repository.source_remotes()) < 2:
                sys.stderr.write('Error. You do not have access to a secure remote to make a pull request for a redacted issue\n')
                sys.stderr.write('Please consult repository administers to gain access to a secure remote to make this fix against\n')
                return None, None
            else:
                source_remote = repository.source_remotes()[-1]
                if args.defaults or Terminal.choose(
                    "Would you like to make a pull request against '{}' instead of '{}'? \n".format(source_remote, original_remote),
                    default='Yes', options=('Yes', 'Cancel')
                ) == 'Cancel':
                    sys.stderr.write("User declined to create a pull request against the secure remote '{}'\n".format(source_remote))
                    return None, None
                remote_repo = repository.remote(name=source_remote)
                print("Making PR against '{}' instead of '{}'".format(source_remote, original_remote))

        if not remote_repo:
            sys.stderr.write("'{}' doesn't have a recognized remote\n".format(repository.root_path))
            return None, None
        return source_remote, remote_repo

    @classmethod
    def check_previous_target(cls, repository, args, source_remote, branch=None):
        branch = branch or repository.branch
        previous_target = repository.config().get('branch.{}.target'.format(branch))
        if previous_target and previous_target != source_remote:
            if args.remote:
                sys.stderr.write("'{}' was previously made against the '{}' remote\n".format(branch, previous_target))
                sys.stderr.write("User over-rode and is now making that PR against '{}'\n".format(args.remote))
            elif args.defaults:
                sys.stderr.write("'{}' was previously made against the '{}' remote\n".format(branch, previous_target))
                sys.stderr.write("Prevailing issue indicates it should be made against '{}'\n".format(source_remote))
                sys.stderr.write("Cannot automatically determine which is correct, canceling pull-request\n")
                return None
            else:
                response = Terminal.choose(
                    "'{}' was previously made against the '{}' remote, but the prevailing issue indicates it should be made against '{}'\n"
                    "Which remote would you like to make your pull request against?".format(branch, previous_target, source_remote),
                    options=('Cancel', 'Use {} (previous)'.format(previous_target), 'Use {} (new)'.format(source_remote)),
                    default='Cancel', numbered=True
                )
                match = re.match(r'Use (.+) \((previous|new)\)', response)
                if not match:
                    sys.stderr.write("User canceled pull-request because new remote target '{}' did not match previous remote target '{}'\n".format(
                        source_remote, previous_target,
                    ))
                    return None
                source_remote = match.group(1)
                print("Making the PR against the '{}' remote".format(source_remote))

        if run(
            [repository.executable(), 'config', 'branch.{}.target'.format(branch), source_remote],
            cwd=repository.root_path, capture_output=True,
        ).returncode:
            sys.stderr.write("Failed to set the target of '{}' to '{}'\n".format(branch, source_remote))
        return source_remote

    @classmethod
    def push_target(cls, repository, remote_repo, source_remote):
        if not isinstance(remote_repo, remote.GitHub):
            return source_remote
        target = 'fork' if source_remote == repository.default_remote else '{}-fork'.format(source_remote)
        if not repository.config().get('remote.{}.url'.format(target)):
            sys.stderr.write("'{}' is not a remote in this repository. Have you run `{} setup` yet?\n".format(
                source_remote, os.path.basename(sys.argv[0]),
            ))
            return None
        return target

    @classmethod
    def existing_pull_request_for(cls, repository, args, remote_repo, source_remote, target, branch=None):
        branch = branch or repository.branch
        existing_pr = None
        if remote_repo.pull_requests:
            user, _ = remote_repo.credentials(required=False)

            log.info("Checking if PR already exists...")
            existing_pr = cls.find_existing_pull_request(repository, remote_repo, branch=branch)
            log.info("PR #{} found.".format(existing_pr.number) if existing_pr else "PR not found.")
            if existing_pr and not existing_pr.opened and not cls.will_reopen_closed_pull_request(args, repository, existing_pr, branch=branch):
                existing_pr = None

            if existing_pr and user and existing_pr.author != user and (args.defaults or Terminal.choose(
                "'{}' is owned by '{}'\nYou can either".format(existing_pr, existing_pr.author),
                options=('Create a new pull request', 'Overwrite PR-{} and assign to yourself'.format(existing_pr.number)),
                default='Create a new pull request',
                numbered=True,
            ) == 'Create a new pull request'):
                if target == source_remote:
                    sys.stderr.write("'{}' already exists on '{}', creating a pull-request would overwrite it\n".format(
                        branch, target,
                    ))
                    return None
                existing_pr = None

            if user and existing_pr and isinstance(remote_repo, remote.GitHub) and existing_pr._metadata.get('full_name'):
                pr_target = existing_pr._metadata['full_name']
                if not pr_target.startswith('{}/'.format(user)):
                    target, repo_name = pr_target.split('/')
                    if '-' in repo_name:
                        target = '{}-{}'.format(target, repo_name.split('-')[-1])
                    base_url = repository.url(name=source_remote)
                    if '://' in base_url:
                        base_url = '/'.join(base_url.split('/')[:3]) + '/'
                    else:
                        base_url = base_url.split(':')[0] + ':'
                    if target not in repository.source_remotes(personal=True) and run(
                        [repository.executable(), 'remote', 'add', target, '{}{}.git'.format(base_url, pr_target)],
                        capture_output=True, cwd=repository.root_path,
                    ).returncode not in [0, 3]:
                        sys.stderr.write("Failed to add '{}' remote\n".format(target))
                        return None
        return existing_pr, target

    @classmethod
    def clear_active_labels(cls, args, existing_pr, unblock=True):
        if not existing_pr or not existing_pr._metadata or not existing_pr._metadata.get('issue'):
            return
        log.info("Checking PR labels for active labels...")
        pr_issue = existing_pr._metadata['issue']
        labels = pr_issue.labels
        did_change = False
        labels_to_add = []
        labels_to_remove = cls.MERGE_LABELS + cls.UNSAFE_MERGE_LABELS
        if unblock:
            labels_to_remove.append(cls.BLOCKED_LABEL)
        if args.ews:
            labels_to_remove.append(cls.SKIP_EWS_LABEL)
        else:
            labels_to_add.append(cls.SKIP_EWS_LABEL)

        for to_add in labels_to_add:
            if to_add not in labels:
                log.info("Adding '{}' to PR #{}...".format(to_add, existing_pr.number))
                labels.append(to_add)
                did_change = True
        for to_remove in labels_to_remove:
            if to_remove in labels:
                log.info("Removing '{}' from PR #{}...".format(to_remove, existing_pr.number))
                labels.remove(to_remove)
                did_change = True
        if did_change:
            pr_issue.set_labels(labels)

    @classmethod
    def push_environment(cls, args):
        push_env = os.environ.copy()
        push_env["VERBOSITY"] = str(args.verbose)
        return push_env

    @classmethod
    def sync_fork(cls, repository, target, branch_point, rebasing, push_env):
        if not target.endswith('fork') or repository.config().get('webkitscmpy.update-fork', 'false') != 'true':
            return

        # If our remote is a GitHub repository, we can use the API and save ourselves a push
        fork_remote = repository.remote(name=target)
        did_update_branch = False
        if isinstance(fork_remote, remote.GitHub):
            log.info("Updating '{}' on '{}'".format(branch_point.branch, fork_remote.url))
            with OutputCapture():
                did_update_branch = fork_remote.request(
                    method='POST', path='merge-upstream',
                    json=dict(branch=branch_point.branch),
                    authenticated=True,
                ) is not None
            if did_update_branch and run([repository.executable(), 'fetch', target, branch_point.branch], cwd=repository.root_path, capture_output=True).returncode:
                sys.stderr.write("Failed to fetch '{}' for '{}.' Error is non fatal, continuing...\n".format(target, branch_point.branch))
        if not did_update_branch and rebasing:
            log.info("Syncing '{}' to remote '{}'".format(branch_point.branch, target))
            if run([repository.executable(), 'push', target, '{branch}:{branch}'.format(branch=branch_point.branch)], cwd=repository.root_path, env=push_env).returncode:
                sys.stderr.write("Failed to sync '{}' to '{}.' Error is non fatal, continuing...\n".format(branch_point.branch, target))

    @classmethod
    def check_pull_request_support(cls, args, remote_repo):
        if not remote_repo.pull_requests:
            sys.stderr.write("'{}' cannot generate pull-requests\n".format(remote_repo.url))
            return False
        if args.draft and not remote_repo.pull_requests.SUPPORTS_DRAFTS:
            sys.stderr.write("'{}' does not support draft pull requests, aborting\n".format(remote_repo.url))
            return False
        return True

    @classmethod
    def create_or_update_pull_request(cls, repository, args, remote_repo, existing_pr, head, commits, base, update_issue, body=None):
        if existing_pr:
            log.info("Updating pull-request for '{}'...".format(head))
            pr = remote_repo.pull_requests.update(
                pull_request=existing_pr,
                title=cls.title_for(commits) if args.update_title else existing_pr.title,
                body=body,
                commits=commits,
                base=base,
                head=head,
                opened=None if existing_pr.opened else True,
                draft=args.draft,
            )
            if not pr:
                sys.stderr.write("Failed to update pull-request '{}'\n".format(existing_pr))
                return None
            print("Updated '{}'!".format(pr))
            return pr

        log.info("Creating pull-request for '{}'...".format(head))
        if not args.update_title:
            sys.stderr.write("'--no-update-title' cannot be used when creating a new pull-request.\n")
            return None
        pr = remote_repo.pull_requests.create(
            title=cls.title_for(commits),
            body=body,
            commits=commits,
            base=base,
            head=head,
            draft=args.draft,
        )
        if not pr:
            sys.stderr.write("Failed to create pull-request for '{}'\n".format(head))
            return None
        print("Created '{}'!".format(pr))
        if cls.is_revert_commit(commits[0]) and update_issue:
            cls.add_comment_to_reverted_commit_bug_tracker(repository, args, pr, commits[0])
        return pr

    @classmethod
    def update_issues_for(cls, repository, args, pr, commits, issues, update_issue):
        issue = issues[0] if issues else None
        radar_issue, not_radar = cls.split_issues(issues)

        commit_class = None
        if repository.classifier and repository.classifier.classes and commits:
            classes = [repository.classifier.classify(commit, repository) for commit in commits]
            classes = [klass for klass in classes if klass]
            if classes and len(classes) == len(commits) and len(set(klass.name for klass in classes)) == 1:
                commit_class = classes[0].name

        if issue and update_issue and isinstance(issue.tracker, radar.Tracker) and not_radar:
            cls.add_comment_to_issue(not_radar, pr, commit_class=commit_class)
        elif issue and update_issue:
            cls.add_comment_to_issue(issue, pr, commit_class=commit_class)

        if radar_issue and update_issue and radar_issue.tracker.radarclient():
            if args.update_radar and radar_issue.state == 'Analyze' and radar_issue.substate in ['Investigate', 'Fix']:
                try:
                    new_state = 'Fix' if pr.draft else 'Review'
                    radar_issue.set_state(state='Analyze', substate=new_state)
                    print(f'Updated {radar_issue.link} to Analyze/{new_state}')
                except radar_issue.tracker.radarclient().exceptions.UnsuccessfulResponseException as e:
                    sys.stderr.write(f'Failed to update {radar_issue.link}:\n')
                    sys.stderr.write(f'{e}\n')

        if issue and pr._metadata and pr._metadata.get('issue'):
            log.info('Syncing PR labels with issue component...')
            pr_issue = pr._metadata['issue']
            project = pr_issue.tracker.name
            component = issue.component
            if pr_issue.component == component or component not in pr_issue.tracker.projects.get(project, {}).get('components', {}):
                component = None
            if component:
                pr_issue.set_component(component=component)
                log.info('Synced PR labels with issue component!')
            else:
                log.info('No label syncing required')
            if not args.ews:
                # Add SKIP_EWS_LABEL if --no-ews argument was passed
                labels = pr_issue.labels
                labels.append(cls.SKIP_EWS_LABEL)
                pr_issue.set_labels(labels)

    @classmethod
    def print_pull_request_url(cls, args, pr):
        if not pr.url:
            return
        print(pr.url)
        if args.open:
            Terminal.open_url(pr.url)

    @classmethod
    def create_pull_request(cls, repository, args, branch_point, callback=None, unblock=True, update_issue=None):
        if update_issue is None:
            update_issue = getattr(args, 'update_issue', True)
        source_remote = args.remote or repository.default_remote
        if not repository.config().get('remote.{}.url'.format(source_remote)):
            sys.stderr.write("'{}' is not a remote in this repository\n".format(source_remote))
            return 1

        rebasing = cls.will_rebase(repository, args)
        if rebasing:
            branch_point = cls.rebase_on_source(repository, source_remote, branch_point)
            if not branch_point:
                return 1

        if not cls.run_checks(repository, args):
            return 1

        commits = list(repository.commits(begin=dict(hash=branch_point.hash), end=dict(branch=repository.branch)))
        issues = [
            issue
            for commit in commits
            for issue in commit.issues
        ]

        unreviewed = re.compile(r'(Unreviewed|Versioning.)', re.IGNORECASE)
        reviewed = re.compile(r'^Reviewed by .+', re.IGNORECASE | re.MULTILINE)
        bad_commits = [c for c in commits if c.message and unreviewed.search(c.message) and reviewed.search(c.message)]

        if bad_commits:
            if len(bad_commits) > 1:
                sys.stderr.write("Multiple commits are marked 'Unreviewed' or 'Versioning' but contain a 'Reviewed by' line, please fix before posting\n")
                return 1
            response = Terminal.choose(
                "Commit message is marked 'Unreviewed' or 'Versioning' but contains a 'Reviewed by' line. Remove it?",
                options=('Yes', 'No'),
                default='Yes',
            )
            if response == 'Yes':
                cleaned = re.sub(r'Reviewed by .+\n?', '', bad_commits[0].message)
                if run([repository.executable(), 'commit', '--amend', '-m', cleaned], cwd=repository.root_path).returncode:
                    sys.stderr.write("Failed to amend commit message\n")
                    return 1
                commits = list(repository.commits(begin={'hash': branch_point.hash}, end={'branch': repository.branch}))

        if not cls.cc_radar(repository, args, issues, update_issue):
            return 1

        source_remote, remote_repo = cls.remote_for_issues(repository, args, source_remote, issues)
        if not remote_repo:
            return 1

        source_remote = cls.check_previous_target(repository, args, source_remote)
        if not source_remote:
            return 1

        target = cls.push_target(repository, remote_repo, source_remote)
        if not target:
            return 1

        found = cls.existing_pull_request_for(repository, args, remote_repo, source_remote, target)
        if not found:
            return 1
        existing_pr, target = found

        # Remove any active labels
        cls.clear_active_labels(args, existing_pr, unblock=unblock)

        set_upstream = (
            args.set_upstream
            if args.set_upstream is not None
            else repository.config().get(
                "webkitscmpy.set-upstream-on-push",
                "false",
            )
            == "true"
        )

        push_env = cls.push_environment(args)
        cls.sync_fork(repository, target, branch_point, rebasing, push_env)

        log.info("Pushing '{}' to '{}'...".format(repository.branch, target))
        if run(
            [repository.executable(), "push", "-f"]
            + (["-u"] if set_upstream else [])
            + [target, repository.branch],
            cwd=repository.root_path,
            env=push_env,
        ).returncode:
            sys.stderr.write("Failed to push '{}' to '{}' (alias of '{}')\n".format(repository.branch, target, repository.url(name=target)))
            sys.stderr.write("Your checkout may be mis-configured, try re-running 'git-webkit setup' or\n")
            sys.stderr.write("your checkout may not have permission to push to '{}'\n".format(repository.url(name=target)))
            return 1

        if args.history or (target != source_remote and args.history is None and args.technique == 'overwrite'):
            regex = re.compile(r'^{}-(?P<count>\d+)$'.format(repository.branch))
            count = max([
                int(regex.match(branch).group('count')) if regex.match(branch) else 0 for branch in
                repository.branches_for(remote=target)
            ] + [0]) + 1

            history_branch = '{}-{}'.format(repository.branch, count)
            log.info("Creating '{}' as a reference branch".format(history_branch))
            if run([
                repository.executable(), 'branch', history_branch, repository.branch,
            ], cwd=repository.root_path).returncode or run([
                repository.executable(), 'push', '-f', target, history_branch,
            ], cwd=repository.root_path, env=push_env).returncode:
                sys.stderr.write("Failed to create and push '{}' to '{}'\n".format(history_branch, target))
                return 1

        if not cls.check_pull_request_support(args, remote_repo):
            return 1

        if args.update_title is None:
            args.update_title = repository.config().get('webkitscmpy.update-title', 'true') == 'true'

        pr = cls.create_or_update_pull_request(
            repository, args, remote_repo, existing_pr,
            head=repository.branch,
            commits=commits,
            base=branch_point.branch,
            update_issue=update_issue,
        )
        if not pr:
            return 1

        cls.update_issues_for(repository, args, pr, commits, issues, update_issue)
        cls.print_pull_request_url(args, pr)

        if callback:
            return callback(pr)
        return 0

    @classmethod
    def uses_per_commit_pull_requests(cls, repository, args):
        if args.per_commit is not None:
            return args.per_commit
        return bool(repository.branch) and repository.config().get(cls.PER_COMMIT_CONFIG.format(repository.branch)) == 'true'

    @classmethod
    def pull_request_branch_name(cls, commit, taken, redact=False):
        issue = commit.issues[0] if commit.issues else None
        redacted = issue and issue.redacted and not getattr(issue.redacted, 'exemption', False)
        if issue and (redact or redacted or issue.tracker.hide_title):
            name = str(issue.id)
        else:
            name = Branch.to_branch_name(commit.message.splitlines()[0] if commit.message else commit.hash[:12])
        name = Branch.truncate_branch_name(Branch.normalize_branch_name(name))

        # Avoid '<name>-<number>', which 'clean' and 'land' consider history branches of '<name>'
        candidate = name
        count = 1
        while candidate in taken:
            count += 1
            candidate = '{}-v{}'.format(name, count)
        return candidate

    @classmethod
    def assign_pull_request_branches(cls, repository, args, branch_point, branch, commits, target, redact=False):
        """Record a pull request branch in the trailers of each commit which doesn't have one yet (fixing
        mislabeled 'Reviewed by' lines while re-writing messages). Returns the commits, oldest first."""
        messages = {}
        details = {commit.hash: repository.commit_details(commit.hash) for commit in commits}

        owners = {}
        for commit in commits:
            head = PullRequestModel.branch_trailer(details[commit.hash]['message'])
            if not head:
                continue
            if head in owners:
                sys.stderr.write("Both '{}' and '{}' are recorded as the commit for '{}'\n".format(
                    owners[head].hash[:12], commit.hash[:12], head,
                ))
                sys.stderr.write("Remove the '{}' trailer from one of their commit messages\n".format(PullRequestModel.BRANCH_TRAILER))
                return None
            owners[head] = commit

        unreviewed = re.compile(r'(Unreviewed|Versioning.)', re.IGNORECASE)
        reviewed = re.compile(r'^Reviewed by .+', re.IGNORECASE | re.MULTILINE)
        for commit in commits:
            message = details[commit.hash]['message']
            if not unreviewed.search(message) or not reviewed.search(message):
                continue
            if Terminal.choose(
                "'{}' is marked 'Unreviewed' or 'Versioning' but contains a 'Reviewed by' line. Remove it?".format(message.splitlines()[0]),
                options=('Yes', 'No'),
                default='Yes',
            ) == 'Yes':
                messages[commit.hash] = re.sub(r'Reviewed by .+\n?', '', message)

        taken = set(repository.branches_for(remote=False)) | set(repository.branches_for(remote=target)) | set(owners.keys())
        owned = set(commit.hash for commit in owners.values())
        for commit in commits:
            if commit.hash in owned:
                continue
            head = cls.pull_request_branch_name(commit, taken, redact=redact)
            taken.add(head)
            messages[commit.hash] = PullRequestModel.add_branch_trailer(messages.get(commit.hash, details[commit.hash]['message']), head)

        if not messages:
            return commits
        log.info('Recording pull request branches in commit messages...')
        try:
            repository.rewrite_messages(branch_point.hash, messages, branch=branch)
        except repository.Exception as error:
            sys.stderr.write('{}\n'.format(error))
            return None
        return list(reversed(list(repository.commits(begin=dict(hash=branch_point.hash), end=dict(branch=branch)))))

    @classmethod
    def is_unchanged(cls, repository, existing_pr, rebuilt, base):
        if not existing_pr or not existing_pr.opened or not existing_pr.hash or existing_pr.base != base:
            return False
        if existing_pr.hash == rebuilt:
            return True
        try:
            return (
                repository.commit_details(existing_pr.hash)['message'] == repository.commit_details(rebuilt)['message']
                and repository.patch_id(existing_pr.hash) == repository.patch_id(rebuilt)
            )
        except repository.Exception:
            # The pull request's head isn't available locally, so we can't compare it
            return False

    @classmethod
    def create_per_commit_pull_requests(cls, repository, args, branch_point, unblock=True):
        update_issue = getattr(args, 'update_issue', True)
        branch = repository.branch
        source_remote = args.remote or repository.default_remote

        rebasing = cls.will_rebase(repository, args)
        if rebasing:
            branch_point = cls.rebase_on_source(repository, source_remote, branch_point)
            if not branch_point:
                return 1

        if not cls.run_checks(repository, args):
            return 1

        commits = list(reversed(list(repository.commits(begin=dict(hash=branch_point.hash), end=dict(branch=branch)))))
        if not commits:
            sys.stderr.write("'{}' has no commits to upload\n".format(branch))
            return 1

        for commit in commits:
            if not cls.cc_radar(repository, args, commit.issues, update_issue):
                return 1

        source_remote, remote_repo = cls.remote_for_issues(
            repository, args, source_remote,
            [issue for commit in commits for issue in commit.issues],
        )
        if not remote_repo:
            return 1
        if not isinstance(remote_repo, remote.GitHub):
            sys.stderr.write("Per-commit pull requests are only supported for GitHub remotes, and '{}' is not one\n".format(source_remote))
            return 1
        source_remote = cls.check_previous_target(repository, args, source_remote, branch=branch)
        if not source_remote:
            return 1
        target = cls.push_target(repository, remote_repo, source_remote)
        if not target:
            return 1
        if not cls.check_pull_request_support(args, remote_repo):
            return 1
        if args.update_title is None:
            args.update_title = repository.config().get('webkitscmpy.update-title', 'true') == 'true'

        commits = cls.assign_pull_request_branches(
            repository, args, branch_point, branch, commits, target,
            redact=source_remote != repository.default_remote,
        )
        if commits is None:
            return 1

        push_env = cls.push_environment(args)
        cls.sync_fork(repository, target, branch_point, rebasing, push_env)

        # Re-create each commit on top of the base branch, without the commits below it
        uploads = []
        conflicted = []
        for commit in commits:
            head = PullRequestModel.branch_trailer(commit.message)
            message = PullRequestModel.strip_branch_trailer(repository.commit_details(commit.hash)['message'])
            try:
                rebuilt = repository.rebuild_commit(commit.hash, branch_point.hash, message=message)
            except repository.MergeConflict as conflict:
                conflicted.append(commit)
                sys.stderr.write("'{}' depends on an earlier commit on '{}', so it cannot be uploaded as its own pull request\n".format(
                    message.splitlines()[0], branch,
                ))
                if conflict.files:
                    sys.stderr.write('    Conflicts in {}\n'.format(', '.join(conflict.files)))
                continue
            except repository.Exception as error:
                sys.stderr.write('{}\n'.format(error))
                return 1

            found = cls.existing_pull_request_for(repository, args, remote_repo, source_remote, target, branch=head)
            if not found:
                return 1
            existing_pr, head_target = found
            unchanged = cls.is_unchanged(repository, existing_pr, rebuilt, branch_point.branch)
            uploads.append(dict(
                commit=commit, head=head, target=head_target,
                existing=existing_pr, pr=existing_pr, created=False, unchanged=unchanged,
                pushed=CommitModel(hash=existing_pr.hash if unchanged else rebuilt, message=message),
            ))

        to_push = [upload for upload in uploads if not upload['unchanged']]
        for upload in to_push:
            cls.clear_active_labels(args, upload['existing'], unblock=unblock)

        for push_target in dict.fromkeys(upload['target'] for upload in to_push):
            pushing = [upload for upload in to_push if upload['target'] == push_target]
            log.info("Pushing {} to '{}'...".format(', '.join("'{}'".format(upload['head']) for upload in pushing), push_target))
            if run(
                [repository.executable(), 'push', '-f', push_target] + [
                    '{}:refs/heads/{}'.format(upload['pushed'].hash, upload['head']) for upload in pushing
                ], cwd=repository.root_path, env=push_env,
            ).returncode:
                sys.stderr.write("Failed to push pull request branches to '{}' (alias of '{}')\n".format(push_target, repository.url(name=push_target)))
                sys.stderr.write("Your checkout may be mis-configured, try re-running 'git-webkit setup' or\n")
                sys.stderr.write("your checkout may not have permission to push to '{}'\n".format(repository.url(name=push_target)))
                return 1

        # Create any new pull requests first, so every pull request can list all of the others
        for upload in uploads:
            if upload['existing']:
                continue
            upload['pr'] = cls.create_or_update_pull_request(
                repository, args, remote_repo, None,
                head=upload['head'], commits=[upload['pushed']], base=branch_point.branch, update_issue=update_issue,
            )
            if not upload['pr']:
                return 1
            upload['created'] = True

        for upload in uploads:
            if upload['unchanged']:
                continue
            if not upload['commit'].issues and update_issue and Tracker.instance():
                sys.stderr.write("'{}' does not reference an issue\n".format(upload['pushed'].message.splitlines()[0]))
            cls.update_issues_for(repository, args, upload['pr'], [upload['commit']], upload['commit'].issues, update_issue)

        # Go back and add a TOC of related PRs. Doing this after the call to
        # update_issues_for helps avoid a race condition with ews-app, where a
        # revision to the PR body made immediately after the PR is created gets
        # dropped when it updates the status bubbles.
        numbers = [upload['pr'].number for upload in uploads]
        for upload in uploads:
            pr = upload['pr']
            related = PullRequestModel.related_body(numbers, current=pr)
            if upload['created']:
                if related and not remote_repo.pull_requests.update(
                    pull_request=pr, title=pr.title, body=related,
                    commits=[upload['pushed']], base=branch_point.branch,
                ):
                    sys.stderr.write("Failed to update pull-request '{}'\n".format(pr))
                    return 1
                continue

            title = cls.title_for([upload['pushed']]) if args.update_title else pr.title
            if upload['unchanged'] and (pr.body or '') == related and pr.title == title:
                print("No changes to '{}'".format(pr))
                continue
            upload['pr'] = cls.create_or_update_pull_request(
                repository, args, remote_repo, pr,
                head=upload['head'], commits=[upload['pushed']], base=branch_point.branch,
                update_issue=update_issue, body=related,
            )
            if not upload['pr']:
                return 1

        for upload in uploads:
            if upload['unchanged']:
                print(upload['pr'].url)
            else:
                cls.print_pull_request_url(args, upload['pr'])

        if conflicted:
            sys.stderr.write("{} of {} commits on '{}' {} not uploaded because {} on earlier commits\n".format(
                len(conflicted), len(commits), branch,
                'was' if len(conflicted) == 1 else 'were',
                'it depends' if len(conflicted) == 1 else 'they depend',
            ))
            sys.stderr.write('Per-commit pull requests require commits which do not depend on each other\n')
            return 1
        return 0

    @classmethod
    def main_per_commit(cls, args, repository, **kwargs):
        branch = repository.branch
        if not branch or not Branch.editable(branch, repository=repository):
            sys.stderr.write("Per-commit pull requests are uploaded from a local development branch, and '{}' is not one\n".format(branch or 'HEAD'))
            sys.stderr.write("Create one with 'git checkout -b {}/<name>'\n".format(Branch.PR_PREFIX))
            return 1
        if args.squash:
            sys.stderr.write("'--squash' cannot be used with per-commit pull requests\n")
            return 1
        version = repository.version()
        if version < repository.MINIMUM_REBUILD_VERSION:
            sys.stderr.write('Per-commit pull requests require git {} or later, but this is git {}\n'.format(
                '.'.join(str(part) for part in repository.MINIMUM_REBUILD_VERSION),
                '.'.join(str(part) for part in version),
            ))
            return 1

        if not cls.check_redaction_args(repository, args):
            return 1
        branch_point = repository.branch_point()
        if not branch_point:
            sys.stderr.write('Failed to determine where pull-request diverged from production branch\n')
            return 1
        source_remote = cls.source_remote_for(repository, args, branch_point)
        if not cls.match_base_to_remote(repository, source_remote, branch_point):
            return 1

        if repository.config().get(cls.PER_COMMIT_CONFIG.format(branch)) != 'true':
            run([repository.executable(), 'config', cls.PER_COMMIT_CONFIG.format(branch), 'true'], capture_output=True, cwd=repository.root_path)
            print("Uploading each commit on '{}' as its own pull request, use '--no-per-commit' to upload the branch as a single pull request".format(branch))

        if args.commit is None:
            args.commit = repository.config().get('webkitscmpy.auto-create-commit') == 'true'
        if args.commit:
            result = cls.create_commit(args, repository, **kwargs)
            if result:
                return result

        return cls.create_per_commit_pull_requests(repository, args, branch_point)

    @classmethod
    def main(cls, args, repository, hooks=None, **kwargs):
        if not isinstance(repository, local.Git):
            sys.stderr.write("Can only '{}' on a native Git repository\n".format(cls.name))
            return 1
        if not cls.check_pull_request_args(repository, args):
            return 1
        if hooks and InstallHooks.hook_needs_update(repository, os.path.join(hooks, 'pre-push')):
            sys.stderr.write("Cannot run a command which invokes `git push` with an out-of-date pre-push hook\n")
            sys.stderr.write("Please re-run `git-webkit setup` to update all local hooks\n")
            return 1

        if cls.uses_per_commit_pull_requests(repository, args):
            return cls.main_per_commit(args, repository, **kwargs)
        if args.per_commit is False and repository.branch and repository.config().get(cls.PER_COMMIT_CONFIG.format(repository.branch)) == 'true':
            run([repository.executable(), 'config', cls.PER_COMMIT_CONFIG.format(repository.branch), 'false'], capture_output=True, cwd=repository.root_path)

        branch_point = cls.pull_request_branch_point(repository, args, **kwargs)
        if not branch_point:
            return 1

        if args.commit is None:
            args.commit = repository.config().get('webkitscmpy.auto-create-commit') == 'true'
        if args.commit:
            result = cls.create_commit(args, repository, **kwargs)
            if result:
                return result
        if args.squash:
            result = Squash.squash_commit(args, repository, branch_point, **kwargs)
            if result:
                return result

        return cls.create_pull_request(repository, args, branch_point)
