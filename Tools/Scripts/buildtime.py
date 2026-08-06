"""Shared implementation of the `bisect-build-time` build-time bisection tool.

Given a commit range and a `measure-build-time` benchmark (default: `clean`), this
drives `git bisect` to find the commit that introduced a build-time regression.

Each commit is timed several times (`--runs`, default 3) and compared to a baseline
(the good endpoint, also timed `--runs` times) with a two-sample Welch's t-test: the
first commit that is *significantly slower* than the baseline (one-sided,
`p <= --alpha`) is the regression point. Comparing distributions rather than single
numbers is what makes the verdict survive the run-to-run variability of a build.

`Tools/Scripts/bisect-build-time` is a thin entry point onto `main()` here. Callers
that need to prepare something before each commit is timed — a companion checkout,
say — build the parser with `build_parser()` and call `run_driver`/`run_harness`
with a `hook` callable:

    hook(commit_sha, writer) -> bool     # False: this commit cannot be measured

On macOS the driver holds sleep assertions (`caffeinate -ims`) for the whole run so
the machine cannot idle-sleep between or during builds.

Everything here is stdlib-only apart from an optional `tqdm` progress bar.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import math
import os
import re
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from types import FrameType
from typing import NamedTuple

try:
    from tqdm import tqdm
except ImportError:  # optional dependency; the bar is skipped without it.
    tqdm = None

log = logging.getLogger('buildtime')

LOG_FORMAT = '%(asctime)s %(levelname)s %(message)s'

MODULE = Path(__file__).resolve()

# `git bisect run` exit-code contract.
EXIT_GOOD = 0
EXIT_BAD = 1
EXIT_SKIP = 125
# 128 + SIGINT: what a shell reports for a process killed by Ctrl-C. Also >= 128,
# which makes `git bisect run` stop rather than blame the commit under test.
EXIT_INTERRUPTED = 130

# Sentinel the harness prints on stdout after each completed timing run; the
# driver detects it in the `git bisect run` stream to advance the progress bar.
TICK = '\x1f__bisect_build_time_tick__\x1f'


# --- Git helpers ------------------------------------------------------------

def git(*args: str, repo: Path | None = None, check: bool = True) -> int:
    """Run a git command in `repo` (default: the current directory) inheriting stdio.

    Returns the exit code, so callers passing `check=False` can react to failure.
    """
    prefix = ('-C', str(repo)) if repo is not None else ()
    return subprocess.run(('git', *prefix, *args), check=check).returncode


def git_output(*args: str, repo: Path | None = None, check: bool = True) -> str:
    """Run a git command in `repo` and return its stdout, stripped."""
    prefix = ('-C', str(repo)) if repo is not None else ()
    proc = subprocess.run(('git', *prefix, *args), text=True, check=check,
                          stdout=subprocess.PIPE)
    return (proc.stdout or '').strip()


def resolve(ref: str, repo: Path | None = None) -> str | None:
    """The commit SHA that `ref` names in `repo`, or None if it doesn't resolve."""
    prefix = ('-C', str(repo)) if repo is not None else ()
    proc = subprocess.run(('git', *prefix, 'rev-parse', '--verify', '--quiet',
                           f'{ref}^{{commit}}'),
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return proc.stdout.strip() or None


def commit_time(ref: str, repo: Path | None = None) -> int:
    """`ref`'s committer date as a unix timestamp — when it landed."""
    return int(git_output('show', '-s', '--format=%ct', ref, repo=repo))


def commit_fields(ref: str, repo: Path | None = None) -> tuple[str, str, str]:
    """(short SHA, YYYY-MM-DD commit date, subject) for `ref`."""
    short, date, subject = git_output('show', '-s', '--format=%h%x09%cs%x09%s',
                                      ref, repo=repo).split('\t', 2)
    return short, date, subject


# WebKit records each commit's identifier (`318105@main`) as a `Canonical link:`
# line at the end of the message. The space in the key means git doesn't see a
# trailer, so it is rewritten into one below. `Identifier` and `git-svn-id` are
# declared as trailer keys as well: a paragraph is only recognized as a trailer
# block if enough of its lines are trailers, and those two pseudo-trailers share
# the block in much of WebKit's history.
TRAILER_CONFIG = ('-c', 'trailer.Canonical-link.key=Canonical-link',
                  '-c', 'trailer.Identifier.key=Identifier',
                  '-c', 'trailer.git-svn-id.key=git-svn-id')
CANONICAL_KEY_RE = re.compile(r'^Canonical link:', re.MULTILINE)
CANONICAL_LINK_RE = re.compile(r'^Canonical-link:\s*\S*?commits\.webkit\.org/(\S+)\s*$',
                               re.MULTILINE)


def commit_identifier(ref: str, repo: Path | None = None) -> str | None:
    """`ref`'s WebKit commit identifier (`318105@main`), or None if it has none.

    Parsed through `git interpret-trailers` rather than grepped out of the message
    so that only a canonical link in the trailer block counts — a commit whose body
    quotes some *other* commit's link has no identifier of its own. Commits that
    never went through commits.webkit.org (local work, or the tool's own fixtures)
    return None.
    """
    message = git_output('show', '-s', '--format=%B', ref, repo=repo, check=False)
    if 'commits.webkit.org/' not in message:
        return None
    prefix = ('-C', str(repo)) if repo is not None else ()
    proc = subprocess.run(('git', *prefix, *TRAILER_CONFIG, 'interpret-trailers', '--parse'),
                          input=CANONICAL_KEY_RE.sub('Canonical-link:', message),
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    match = CANONICAL_LINK_RE.search(proc.stdout or '')
    return match.group(1) if match else None


def describe(ref: str, repo: Path | None = None, width: int = 60) -> str:
    """One-line commit description for logging: `abc1234 (2026-07-27) subject`."""
    short, date, subject = commit_fields(ref, repo=repo)
    if len(subject) > width:
        subject = subject[:width - 3] + '...'
    return f'{short} ({date}) {subject}'


def current_ref(repo: Path | None = None) -> str:
    """The branch name if `repo` is on one, else its commit SHA (detached HEAD)."""
    branch = git_output('symbolic-ref', '--quiet', '--short', 'HEAD',
                        repo=repo, check=False)
    return branch or git_output('rev-parse', 'HEAD', repo=repo)


def require_clean_worktree(repo: Path | None = None, *, what: str | None = None,
                           hint: str = 'commit or stash them before bisecting',
                           fatal: bool = True) -> bool:
    """True when tracked files are unmodified; complain (and by default abort) if not.

    `git bisect` refuses to run with a dirty tree, and any checkout this tool makes
    would fail the same way. Untracked files are fine.
    """
    status = git_output('status', '--porcelain', '--untracked-files=no', repo=repo)
    if not status:
        return True
    message = (f'{what or repo or "Working tree"} has uncommitted changes to tracked '
               f'files; {hint}:\n{status}')
    if fatal:
        sys.exit(message)
    log.warning(message)
    return False


# --- Progress bar (optional tqdm) -------------------------------------------

class _TqdmLoggingHandler(logging.Handler):
    """Route log records through `tqdm.write` so they don't corrupt the bar."""

    def emit(self, record: logging.LogRecord) -> None:
        try:
            tqdm.write(self.format(record), file=sys.stderr)
        except Exception:
            self.handleError(record)


class Progress:
    """An optional tqdm bar over the expected number of measure-build-time runs.

    Disabled (bar is None) when tqdm is unavailable or progress is turned off, in
    which case every method is a cheap no-op except `write`, which falls back to
    plain stdout so output pass-through is unchanged.
    """

    def __init__(self, total: int, enabled: bool):
        self.bar = None
        self._saved_handlers: list[logging.Handler] = []
        if enabled and tqdm is not None:
            self.bar = tqdm(total=total, unit='run', desc='timing runs',
                            file=sys.stderr, leave=True)
            root = logging.getLogger()
            self._saved_handlers = root.handlers[:]
            for h in self._saved_handlers:
                root.removeHandler(h)
            handler = _TqdmLoggingHandler()
            handler.setFormatter(logging.Formatter(LOG_FORMAT))
            root.addHandler(handler)
            signal.signal(signal.SIGWINCH, self.handle_resize)

    def tick(self, n: int = 1) -> None:
        if self.bar is not None:
            self.bar.update(n)

    def write(self, text: str) -> None:
        if self.bar is not None:
            tqdm.write(text, end='', file=sys.stdout)
        else:
            sys.stdout.write(text)
            sys.stdout.flush()

    def handle_resize(self, signum: int, frame: FrameType | None) -> None:
        if self.bar is not None:
            ncols, _ = shutil.get_terminal_size()
            self.bar.ncols = ncols
            self.bar.refresh()

    def close(self) -> None:
        if self.bar is not None:
            signal.signal(signal.SIGWINCH, signal.SIG_DFL)
            self.bar.close()
            self.bar = None
            root = logging.getLogger()
            for h in root.handlers[:]:
                root.removeHandler(h)
            for h in self._saved_handlers:
                root.addHandler(h)


# --- Running the benchmark --------------------------------------------------

def extract_time(results: dict, test_name: str) -> float | None:
    """Find `test_name`'s wall time (seconds) anywhere in a results tree.

    `measure-build-time` places `clean` at the top level of a metric's `tests`
    map, but nests incremental subtests one level deeper under an `incremental`
    group. Recurse to handle both without hard-coding the layout.
    """
    def search(node: object) -> float | None:
        if not isinstance(node, dict):
            return None
        tests = node.get('tests')
        if isinstance(tests, dict):
            target = tests.get(test_name)
            if isinstance(target, dict):
                current = target.get('metrics', {}).get('Time', {}).get('current')
                if isinstance(current, list) and current:
                    return current[0] / 1000.0
            for child in tests.values():
                found = search(child)
                if found is not None:
                    return found
        return None

    for metric in results.values():
        found = search(metric)
        if found is not None:
            return found
    return None


def tests_for(test_name: str) -> list[str]:
    """Benchmarks to run: incremental tests depend on a preceding clean build."""
    return ['clean'] if test_name == 'clean' else ['clean', test_name]


def stdout_writer(text: str) -> None:
    """Default output sink: echo to this process's stdout."""
    sys.stdout.write(text)
    sys.stdout.flush()


def stream_command(command: list[str], writer) -> int:
    """Run `command`, streaming its combined output through `writer`; return its rc.

    Output is captured rather than inherited so a caller can route it through the
    progress bar (`Progress.write`) and keep the bar intact. Runs with stdin closed
    so nothing blocks on a prompt (e.g. the `clean` test's confirmation).
    """
    proc = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, bufsize=1)
    assert proc.stdout is not None
    # readline (not `for line in`) avoids the text-stream read-ahead buffer, so
    # output reaches the bar as it is produced.
    for line in iter(proc.stdout.readline, ''):
        writer(line)
    proc.stdout.close()
    return proc.wait()


def run_benchmark(benchmark: Path, test_name: str, forwarded: list[str],
                  source_dir: Path, build_dir: Path, writer=None) -> float | None:
    """Run `measure-build-time` for one benchmark; return its wall seconds.

    `source_dir` and `build_dir` come ahead of `forwarded`, so a forwarded value
    still overrides them: the benchmark works both out from its own location, which
    the copy outside the checkout doesn't have, and the build belongs somewhere
    throwaway rather than in the engineer's build directory.

    Returns None if the build fails (so the caller can skip the commit).
    """
    if writer is None:
        writer = stdout_writer
    with tempfile.NamedTemporaryFile(prefix='bisect-build-time-', suffix='.json') as out:
        command = [
            str(benchmark),
            '--source-dir', str(source_dir),
            '--build-dir', str(build_dir),
            *forwarded,
            '--tests', *tests_for(test_name),
            '--no-keep-going',
            '--output', out.name,
        ]
        log.info('Running: %s', ' '.join(command))
        returncode = stream_command(command, writer)
        if returncode != 0:
            log.error('measure-build-time failed with exit code %d', returncode)
            return None
        try:
            results = json.load(open(out.name))
        except (json.JSONDecodeError, OSError) as e:
            log.error('Could not read results JSON: %s', e)
            return None
    seconds = extract_time(results, test_name)
    if seconds is None:
        log.error('No timing found for test %r in results', test_name)
    return seconds


# --- Profiling cache --------------------------------------------------------
#
# Timings are expensive (minutes each) and perfectly reusable as long as nothing
# about the build changed, so they are cached across runs. Re-running a bisect
# after widening the range then only builds the commits it hasn't seen.

CACHE_NAME = 'webkit-build-time-cache.jsonl'

# Warn when cached and fresh samples for one commit disagree by more than this;
# it means conditions drifted and the comparison is on shaky ground.
CACHE_DRIFT_WARN = 0.10


class Timings(NamedTuple):
    """Wall times for one commit, and how many of them came from the cache."""
    values: list[float]
    cached: int


def cache_signature(test_name: str, forwarded: list[str], tag: str | None = None) -> str:
    """Identify the conditions a timing was measured under.

    A time is only comparable to another measured the same way, so reuse is scoped
    to this signature. The host matters most: reusing a fast machine's time on a
    slow one misclassifies and sends the bisect down the wrong branch, which is
    worse than re-measuring, so the cache is inherently machine-local. `tag` is the
    manual escape hatch (`--cache-tag`) for changes we can't see, like a toolchain
    upgrade.
    """
    payload = json.dumps({'test': test_name, 'args': forwarded,
                          'host': socket.gethostname(), 'tag': tag}, sort_keys=True)
    return hashlib.sha256(payload.encode()).hexdigest()[:12]


def default_cache_path(repo: Path | None = None) -> Path | None:
    """Where to keep the cache for the repo being bisected.

    Inside the git directory: it survives the `clean` test deleting the build
    directory, never shows up in `git status`, and is per-worktree. Deliberately
    *not* `--git-common-dir`, which linked worktrees share while having different
    build directories.
    """
    try:
        git_dir = git_output('rev-parse', '--absolute-git-dir', repo=repo)
    except subprocess.CalledProcessError:
        return None
    return Path(git_dir) / CACHE_NAME if git_dir else None


def load_cache(path: str | Path | None, signature: str, context: str = '', *,
               max_age_days: float = 0) -> dict[str, list[float]]:
    """Samples per commit from `path`, for this signature and context only.

    Records accumulate rather than overwrite, so repeated runs make a commit's
    sample set stronger. Entries older than `max_age_days` (0: no limit) are
    ignored — absolute build times drift as the machine and toolchain change. A
    malformed line is skipped rather than fatal; the file is disposable.
    """
    samples: dict[str, list[float]] = {}
    if not path:
        return samples
    cutoff = time.time() - max_age_days * 86400 if max_age_days else 0
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if rec.get('signature') != signature or rec.get('context', '') != context:
                    continue
                if cutoff and rec.get('timestamp', 0) < cutoff:
                    continue
                values = rec.get('samples')
                if isinstance(values, list) and values:
                    samples.setdefault(rec.get('commit', ''), []).extend(values)
    except OSError:
        return samples
    return samples


def cache_age(path: str | Path | None, signature: str, context: str,
              commit: str) -> float | None:
    """Seconds since the newest cached record for `commit`, or None if there is none."""
    newest = None
    try:
        with open(path) as f:  # type: ignore[arg-type]
            for line in f:
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if (rec.get('commit') == commit and rec.get('signature') == signature
                        and rec.get('context', '') == context):
                    stamp = rec.get('timestamp', 0)
                    newest = stamp if newest is None else max(newest, stamp)
    except (OSError, TypeError):
        return None
    return None if newest is None else time.time() - newest


def append_cache(path: str | Path | None, commit: str, sample: float, *,
                 signature: str, context: str, test_name: str) -> None:
    """Append one measured sample. One record per sample, written as it is measured,
    so an interrupted run keeps whatever it managed to build."""
    if not path:
        return
    record = {
        'commit': commit,
        'signature': signature,
        'context': context,
        'samples': [sample],
        'test': test_name,
        'host': socket.gethostname(),
        'timestamp': int(time.time()),
    }
    try:
        with open(path, 'a') as f:
            f.write(json.dumps(record) + '\n')
    except OSError as e:
        log.warning('Could not write to the profiling cache %s: %s', path, e)


def format_age(seconds: float | None) -> str:
    """Compact age for logging: `3h`, `2d`, `45m`."""
    if seconds is None:
        return '?'
    if seconds < 3600:
        return f'{seconds / 60:.0f}m'
    if seconds < 86400:
        return f'{seconds / 3600:.0f}h'
    return f'{seconds / 86400:.1f}d'


def samples_for(args: argparse.Namespace, forwarded: list[str], commit: str, *,
                context: str = '', on_sample=None, writer=None) -> Timings | None:
    """Timings for `commit`: cached ones plus however many more `--runs` wants.

    The single place timings come from, so calibration and every bisect step share
    the same cache behaviour. Returns None if a build failed (the caller skips the
    commit). `on_sample` fires for cached samples too, so the progress bar total
    still means something when a run is mostly cache hits.
    """
    cached: list[float] = []
    if args.cache and not args.refresh:
        cached = load_cache(args.cache, args.signature, context,
                            max_age_days=args.cache_max_age).get(commit, [])
    needed = max(0, args.runs - len(cached))
    if cached:
        log.info('cache: %d sample(s) for %s (newest %s old); measuring %d more',
                 len(cached), commit[:12],
                 format_age(cache_age(args.cache, args.signature, context, commit)),
                 needed)
        for _ in cached:
            if on_sample is not None:
                on_sample()

    fresh: list[float] = []
    for i in range(1, needed + 1):
        if needed > 1:
            log.info('Timing run %d/%d', i, needed)
        seconds = run_benchmark(args.measure_build_time, args.test, forwarded,
                                args.source_dir, args.build_dir, writer=writer)
        if on_sample is not None:
            on_sample()
        if seconds is None:
            return None
        fresh.append(seconds)
        if args.cache:
            append_cache(args.cache, commit, seconds, signature=args.signature,
                         context=context, test_name=args.test)

    if cached and fresh:
        # Free drift detection: these two sets are about to be compared with each
        # other's neighbours, so a big gap means the cache is misleading.
        old, new = statistics.mean(cached), statistics.mean(fresh)
        if old and abs(new - old) / old > CACHE_DRIFT_WARN:
            log.warning('Cached samples for %s average %.1fs but fresh ones average '
                        '%.1fs (%.0f%% apart); conditions may have drifted — consider '
                        '--refresh or --no-cache.',
                        commit[:12], old, new, 100 * abs(new - old) / old)
    return Timings(cached + fresh, len(cached))


def show_cache(args: argparse.Namespace) -> int:
    """Print the cache entries matching this run's signature, oldest first."""
    if not args.cache:
        print('Profiling cache is disabled.')
        return 0
    print(f'Profiling cache: {args.cache}')
    print(f'Signature: {args.signature}  (test: {args.test}, host: '
          f'{socket.gethostname()})')
    if not Path(args.cache).exists():
        print('  (nothing cached yet)')
        return 0
    rows = []
    try:
        with open(args.cache) as f:
            for line in f:
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if rec.get('signature') != args.signature:
                    continue
                rows.append(rec)
    except OSError as e:
        print(f'  (unreadable: {e})')
        return 0
    if not rows:
        print('  (no entries for this signature)')
        return 0
    merged: dict[tuple[str, str], list] = {}
    for rec in rows:
        key = (rec.get('commit', ''), rec.get('context', ''))
        entry = merged.setdefault(key, [[], 0])
        entry[0].extend(rec.get('samples') or [])
        entry[1] = max(entry[1], rec.get('timestamp', 0))
    print()
    print(f'    {"COMMIT":<12}  {"CONTEXT":<12}  {"N":>3}  {"MEAN":>8}  AGE')
    for (commit, context), (samples, stamp) in sorted(merged.items(),
                                                      key=lambda kv: kv[1][1]):
        age = format_age(time.time() - stamp) if stamp else '?'
        print(f'    {commit[:12]:<12}  {(context or "—")[:12]:<12}  {len(samples):>3}  '
              f'{statistics.mean(samples):>7.1f}s  {age}')
    return 0


# --- Statistics (stdlib only) -----------------------------------------------

# The t-test below needs the regularized incomplete beta function
#
#     I_x(a, b) = B(x; a, b) / B(a, b),   B(x; a, b) = ∫₀ˣ u^(a-1) (1-u)^(b-1) du
#
# for which there is no closed form at the non-integer degrees of freedom Welch's
# test produces, so it is summed as a series. Splitting off a factor of (1-u) and
# integrating by parts gives, after dividing through by B(a, b),
#
#     I_x(a, b) = I_x(a+1, b) + x^a (1-x)^b / (a · B(a, b))
#
# and I_x(a+k, b) → 0 as k grows (for x < 1), so iterating that relation and
# collecting the ratio of consecutive leftover terms leaves
#
#     I_x(a, b) = [x^a (1-x)^b / (a · B(a, b))] · Σ_{n≥0} [(a+b)ₙ / (a+1)ₙ] xⁿ
#
# writing (q)ₙ for the rising factorial q(q+1)...(q+n-1). Every term of the sum is
# positive for a, b > 0 and 0 < x < 1, so it accumulates without cancellation.

# How near 1 an argument the series is asked to sum: the term count below goes as
# 1/(1-x), and this bounds it at a few thousand.
SERIES_LIMIT = 0.99


def _log_beta(a: float, b: float) -> float:
    """log B(a, b), through lgamma so the gamma values themselves can't overflow."""
    return math.lgamma(a) + math.lgamma(b) - math.lgamma(a + b)


def _beta_tail(a: float, b: float, x: float) -> float:
    """I_x(a, b) for 0 < x < 1, summed straight from the series derived above.

    Consecutive terms satisfy tₙ₊₁ = tₙ · x · (a+b+n)/(a+1+n), a ratio that moves
    monotonically toward x, so the sum is geometric in its tail and takes on the
    order of log(EPS)/log(x) terms. Keeping x away from 1 is the caller's job;
    MAX_TERMS is a backstop for one that doesn't, and the sum then stops at
    whatever accuracy it has reached rather than spinning.
    """
    MAX_TERMS, EPS = 20_000, 1e-16
    total = term = 1.0
    for n in range(MAX_TERMS):
        ratio = x * (a + b + n) / (a + 1.0 + n)
        term *= ratio
        total += term
        r = ratio if ratio > x else x
        if term * r < EPS * total * (1.0 - r):
            break
    log_front = a * math.log(x) + b * math.log1p(-x) - math.log(a) - _log_beta(a, b)
    return math.exp(log_front) * total


def regularized_incomplete_beta(a: float, b: float, x: float) -> float:
    """I_x(a, b): the share of the beta(a, b) distribution lying below x (a, b > 0)."""
    if x <= 0.0:
        return 0.0
    if x >= 1.0:
        return 1.0
    lower_tail_is_smaller = x * (a + b) <= a
    if x <= SERIES_LIMIT and (lower_tail_is_smaller or 1.0 - x > SERIES_LIMIT):
        return min(1.0, _beta_tail(a, b, x))
    return max(0.0, 1.0 - _beta_tail(b, a, 1.0 - x))


def welch_ttest(a: list[float], b: list[float]) -> tuple[float, float]:
    """Two-sample Welch's t-test comparing `a` against `b` (each len >= 2).

    Returns (t, p_two_sided); t > 0 means mean(a) > mean(b).

    Under the null hypothesis t is drawn from Student's t distribution with the
    Welch–Satterthwaite degrees of freedom ν computed below. Substituting
    u = ν/(ν + w²) into that distribution's two-sided tail 2·∫_|t|^∞ fν(w) dw
    turns it into an incomplete beta integral whose normalization is exactly
    B(ν/2, 1/2), leaving p = I_{ν/(ν+t²)}(ν/2, 1/2).
    """
    na, nb = len(a), len(b)
    ma, mb = statistics.mean(a), statistics.mean(b)
    sa = statistics.variance(a) / na
    sb = statistics.variance(b) / nb
    denom = sa + sb
    if denom == 0.0:
        # No variance in either sample: decide purely by the means.
        if ma == mb:
            return 0.0, 1.0
        return (math.inf if ma > mb else -math.inf), 0.0
    t = (ma - mb) / math.sqrt(denom)
    df = denom * denom / (sa * sa / (na - 1) + sb * sb / (nb - 1))
    p = regularized_incomplete_beta(df / 2.0, 0.5, df / (df + t * t))
    return t, p


def format_pvalue(p: float | None) -> str:
    """Compact p-value: scientific for tiny values, 3 decimals otherwise."""
    if p is None:
        return '—'
    return f'{p:.1e}' if p < 1e-3 else f'{p:.3f}'


def record_measurement(journal: str | None, commit: str,
                       samples: list[float] | None, pvalue: float | None = None,
                       cached: int = 0) -> None:
    """Append one commit's measured wall times to the shared journal (JSON lines).

    The per-commit harness runs in its own process, so it can't return timings
    to the driver directly; each invocation logs here and the driver reads the
    journal back to print the final summary. `samples` is None for a skipped
    (build-failed) commit; `pvalue` is the t-test result against the baseline, or
    None for the baseline commit itself; `cached` is how many of the samples came
    from the profiling cache. Raw samples are preserved so verdicts can be
    recomputed at summary time.
    """
    if not journal:
        return
    with open(journal, 'a') as f:
        f.write(json.dumps({'commit': commit, 'samples': samples, 'pvalue': pvalue,
                            'cached': cached}) + '\n')


# --- Harness mode (invoked once per commit by `git bisect run`) -------------

def run_harness(args: argparse.Namespace, forwarded: list[str], *, hook=None,
                cache_context=None) -> int:
    """Time the commit `git bisect` has checked out and classify it."""
    commit = git_output('rev-parse', 'HEAD')
    if not Path(args.measure_build_time).exists():
        # Skip rather than exit nonzero: `git bisect run` reads exit codes 1-124 as
        # "bad", so a configuration error would otherwise blame an innocent commit.
        record_measurement(args.journal, commit, None, None)
        log.error('measure-build-time not found at %s; skipping this commit.',
                  args.measure_build_time)
        return EXIT_SKIP
    # `git bisect` checked this commit out for us, so this is where anything the
    # build depends on gets prepared for it.
    if hook is not None and not hook(commit, stdout_writer):
        record_measurement(args.journal, commit, None, None)
        log.warning('Skipping this commit (checkout hook failed).')
        return EXIT_SKIP
    # Asked after the hook, so it describes what will actually be built.
    context = (cache_context(commit) or '') if cache_context is not None else ''
    on_sample = (lambda: print(TICK, flush=True)) if args.progress_ticks else None
    timings = samples_for(args, forwarded, commit, context=context,
                          on_sample=on_sample)
    if timings is None:
        record_measurement(args.journal, commit, None, None)
        log.warning('Skipping this commit (build failed or no timing).')
        return EXIT_SKIP
    samples = timings.values

    # Significantly slower than the baseline (one-sided) is bad.
    baseline = [float(x) for x in args.baseline.split(',') if x]
    t, pvalue = welch_ttest(samples, baseline)
    is_bad = t > 0 and pvalue <= args.alpha
    log.info('%s: mean %.1fs over %d runs, p=%s vs baseline -> %s',
             args.test, statistics.mean(samples), len(samples),
             format_pvalue(pvalue), 'BAD' if is_bad else 'GOOD')

    record_measurement(args.journal, commit, samples, pvalue, cached=timings.cached)
    return EXIT_BAD if is_bad else EXIT_GOOD


# --- Driver mode ------------------------------------------------------------

def checkout_for_measurement(ref: str, hook, writer, cache_context=None) -> tuple[str, str]:
    """Check out `ref` for measurement; return (commit sha, cache context).

    Runs the checkout hook (if any) so anything the build depends on is prepared
    before the benchmark runs, then asks `cache_context` what that produced. Exits
    on hook failure: unlike a bisect step, an endpoint we cannot prepare cannot
    simply be skipped.
    """
    git('checkout', '--quiet', ref)
    sha = git_output('rev-parse', 'HEAD')
    if hook is not None and not hook(sha, writer):
        sys.exit(f'Checkout hook failed for {ref} ({sha[:12]}); cannot measure '
                 f'this endpoint.')
    context = (cache_context(sha) or '') if cache_context is not None else ''
    return sha, context


def prepare_baseline(args: argparse.Namespace, forwarded: list[str],
                     progress: Progress, hook=None,
                     cache_context=None) -> tuple[list[float], str]:
    """Measure both endpoints `--runs` times; return (baseline_samples, good_sha).

    The good endpoint is the baseline. The bad endpoint is measured too, as a
    sanity check that the range actually contains a detectable regression. The
    caller restores the original ref; this leaves HEAD on the bad endpoint.
    """
    log.info('Measuring baseline over %d runs at each endpoint...', args.runs)
    good_sha, good_context = checkout_for_measurement(args.good, hook, progress.write,
                                                      cache_context)
    good = samples_for(args, forwarded, good_sha, context=good_context,
                       on_sample=progress.tick, writer=progress.write)
    if good is not None:
        record_measurement(args.journal, good_sha, good.values, None, good.cached)
    bad_sha, bad_context = checkout_for_measurement(args.bad, hook, progress.write,
                                                    cache_context)
    bad = samples_for(args, forwarded, bad_sha, context=bad_context,
                      on_sample=progress.tick, writer=progress.write)

    if good is None or bad is None:
        sys.exit('Calibration failed: an endpoint build did not produce timings. '
                 'Fix the build or reduce the range.')
    baseline, bad_samples = good.values, bad.values
    t, p = welch_ttest(bad_samples, baseline)
    record_measurement(args.journal, bad_sha, bad_samples, p, bad.cached)
    log.info('Baseline good=%.1fs bad=%.1fs (p=%s)',
             statistics.mean(baseline), statistics.mean(bad_samples), format_pvalue(p))
    if not (t > 0 and p <= args.alpha) and not args.force:
        sys.exit(f'Bad endpoint is not significantly slower than good '
                 f'(p={format_pvalue(p)} at alpha={args.alpha}); no detectable '
                 f'regression in range. Increase --runs, widen the range, or pass '
                 f'--force to bisect anyway.')
    return baseline, good_sha


FIRST_BAD_RE = re.compile(r'^([0-9a-f]{7,40}) is the first bad commit', re.MULTILINE)


def expected_runs(good: str, bad: str, runs: int) -> int:
    """Estimate total measure-build-time invocations for the progress bar.

    git bisect tests ~log2(range) commits, each timed `runs` times, plus the two
    baseline endpoints (also `runs` each). Approximate — skips and uneven splits
    vary the real count; tqdm tolerates over/undershoot.
    """
    try:
        n = int(git_output('rev-list', '--count', f'{good}..{bad}'))
    except (ValueError, subprocess.CalledProcessError):
        n = 0
    steps = math.ceil(math.log2(n)) if n >= 1 else 0
    return runs * (steps + 2)


def run_bisect(harness: list[str], progress: Progress) -> tuple[int, str | None]:
    """Run `git bisect run`, streaming its output, and return (rc, first_bad_sha).

    Per-run tick sentinels emitted by the harness advance the progress bar and
    are stripped from the echoed output.
    """
    proc = subprocess.Popen(('git', 'bisect', 'run', *harness),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, bufsize=1)
    captured: list[str] = []
    assert proc.stdout is not None
    for line in proc.stdout:
        if line.rstrip('\n') == TICK:
            progress.tick()
            continue
        progress.write(line)
        captured.append(line)
    proc.wait()
    match = FIRST_BAD_RE.search(''.join(captured))
    return proc.returncode, (match.group(1) if match else None)


def print_summary(journal: str, first_bad: str | None, test_name: str, bad: str, *,
                  runs: int, alpha: float, baseline_commit: str | None,
                  cache: str | Path | None = None,
                  interrupted: bool = False) -> None:
    """Print a table of every commit measured, highlighting the first bad one."""
    measured: dict[str, dict] = {}
    try:
        with open(journal) as f:
            for line in f:
                line = line.strip()
                if line:
                    rec = json.loads(line)
                    measured[rec['commit']] = rec
    except (OSError, json.JSONDecodeError, KeyError):
        return
    if not measured:
        return

    first_bad_full = (resolve(first_bad) or '') if first_bad else ''
    baseline_full = (resolve(baseline_commit) or '') if baseline_commit else ''
    baseline_mean = None
    if baseline_full in measured and measured[baseline_full].get('samples'):
        baseline_mean = statistics.mean(measured[baseline_full]['samples'])

    # Order commits by ancestry (oldest first) so the fast -> slow transition is
    # visible. `git rev-list` lists newest first; every measured commit is an
    # ancestor of `bad`, so a higher index means older.
    ancestry = git_output('rev-list', '--topo-order', bad).splitlines()
    rank = {sha: i for i, sha in enumerate(ancestry)}

    rows = []
    for commit, rec in measured.items():
        samples = rec.get('samples')
        pvalue = rec.get('pvalue')
        short, date, subject = commit_fields(commit)
        # Prefer the commit identifier — it is what WebKit bug reports and
        # commits.webkit.org URLs quote. Commits that never landed upstream (local
        # work) have none, so fall back to the date.
        ident = commit_identifier(commit) or date
        if not samples:
            nstr, meanstr, pstr, verdict, cachestr = '0', 'skip', '—', 'skip', '—'
        else:
            mean = statistics.mean(samples)
            nstr, meanstr = str(len(samples)), f'{mean:.1f}s'
            hits = rec.get('cached') or 0
            cachestr = f'{hits}/{len(samples)}' if hits else '—'
            if commit == baseline_full:
                pstr, verdict = 'base', 'base'
            else:
                pstr = format_pvalue(pvalue)
                verdict = ('bad' if (pvalue is not None and pvalue <= alpha
                                     and baseline_mean is not None and mean > baseline_mean)
                           else 'good')
        rows.append((rank.get(commit, -1), short, commit, ident, nstr, meanstr, pstr,
                     verdict, subject, cachestr))
    rows.sort(reverse=True)  # oldest (highest rank) first

    sha_w = max(len('COMMIT'), *(len(r[1]) for r in rows))
    ident_w = max(len('IDENTIFIER'), *(len(r[3]) for r in rows))
    mean_w = max(len('MEAN'), *(len(r[5]) for r in rows))
    p_w = max(len('P-VALUE'), *(len(r[6]) for r in rows))
    verdict_w = max(len('VERDICT'), *(len(r[7]) for r in rows))
    cache_w = max(len('CACHED'), *(len(r[9]) for r in rows))

    bold, red, reset = ('\033[1m', '\033[31m', '\033[0m') if sys.stdout.isatty() else ('', '', '')

    print()
    # Say so when the run ended early, so a partial table isn't read as the whole
    # range having been tested.
    partial = ', interrupted' if interrupted else ''
    print(f'Build-time bisect summary (test: {test_name}, runs: {runs}, '
          f'alpha: {alpha}{partial})')
    print()
    print(f'    {"COMMIT":<{sha_w}}  {"IDENTIFIER":<{ident_w}}  {"RUNS":>4}  '
          f'{"MEAN":>{mean_w}}  {"P-VALUE":>{p_w}}  {"CACHED":>{cache_w}}  '
          f'{"VERDICT":<{verdict_w}}  SUBJECT')
    for (_rank, short, commit, ident, nstr, meanstr, pstr, verdict, subject,
         cachestr) in rows:
        is_first_bad = bool(first_bad_full) and commit == first_bad_full
        marker = '>>> ' if is_first_bad else '    '
        subj = subject if len(subject) <= 60 else subject[:57] + '...'
        line = (f'{marker}{short:<{sha_w}}  {ident:<{ident_w}}  {nstr:>4}  '
                f'{meanstr:>{mean_w}}  {pstr:>{p_w}}  {cachestr:>{cache_w}}  '
                f'{verdict:<{verdict_w}}  {subj}')
        if is_first_bad:
            line = f'{bold}{red}{line}{reset}  <- first bad commit'
        print(line)
    print()

    if baseline_mean is not None:
        base_short = next((r[1] for r in rows if r[2] == baseline_full), baseline_full[:9])
        print(f'Baseline: {base_short} (good endpoint), '
              f'{baseline_mean:.1f}s mean over {len(measured[baseline_full]["samples"])} '
              f'runs')
    reused = sum(rec.get('cached') or 0 for rec in measured.values())
    total = sum(len(rec.get('samples') or []) for rec in measured.values())
    if cache and reused:
        print(f'Cache: reused {reused} of {total} samples from {cache}')
    first_row = next((r for r in rows if r[2] == first_bad_full), None)
    if first_row:
        print(f'First bad commit: {first_row[1]} ({first_row[3]}) {first_row[8]}')
    else:
        print('No first bad commit identified.')


def harness_copy(entry_script: Path) -> tuple[Path, Path]:
    """Copy `entry_script` and this module to a temp dir; return (dir, script).

    `git bisect` checks out commits where these files are older or missing, and the
    driver re-invokes the entry script once per commit as the harness. Running it
    from outside the checkout means every commit is measured by this version of the
    code — and a missing harness would otherwise exit 2, which `git bisect run`
    reads as "bad". The module is copied alongside so the copy's own directory (its
    `sys.path[0]`) satisfies the import.
    """
    directory = Path(tempfile.mkdtemp(prefix='bisect-build-time-harness-'))
    script = directory / entry_script.name
    shutil.copy2(entry_script, script)
    shutil.copy2(MODULE, directory / MODULE.name)
    return directory, script


def repo_root() -> Path | None:
    """The top level of the checkout being bisected, or None if there isn't one."""
    toplevel = git_output('rev-parse', '--show-toplevel', check=False)
    return Path(toplevel).resolve() if toplevel else None


def benchmark_copy(benchmark: Path, directory: Path, root: Path | None) -> Path | None:
    """Copy `measure-build-time` into `directory`; None if there is nothing to pin.

    `git bisect` rewinds the whole tree, the benchmark script included, so without
    this a run launched from a tree that has changes to `measure-build-time` would
    measure each commit with whatever version that commit happens to carry: local
    changes silently absent, and commits predating a test or fix it needs failing
    outright. Copying it out pins one version across the range, the same reason the
    harness copy exists.

    A benchmark that lives outside the checkout is already beyond `git bisect`'s
    reach, so it is left where it is.
    """
    benchmark = benchmark.resolve()
    if root is None or not benchmark.is_relative_to(root):
        log.info('%s is outside the checkout being bisected; measuring with it as-is.',
                 benchmark)
        return None
    copy = directory / benchmark.name
    shutil.copy2(benchmark, copy)
    log.info('Measuring every commit with this checkout\'s %s, copied to %s.',
             benchmark.name, copy)
    return copy


def run_driver(args: argparse.Namespace, forwarded: list[str], *, entry_script: Path,
               hook=None, harness_args=(), cache_context=None) -> int:
    """Calibrate the endpoints, then drive `git bisect run` over the range.

    `entry_script` is re-invoked once per commit in harness mode (from a temp copy).
    `hook` prepares each commit before it is timed, `cache_context` reports what that
    produced so cached timings are never reused under different conditions, and
    `harness_args` are extra arguments `entry_script` needs to reconstruct itself in
    the harness process.
    """
    benchmark = Path(args.measure_build_time)
    if not benchmark.exists():
        sys.exit(f'measure-build-time not found at {benchmark}; pass '
                 f'--measure-build-time or set $MEASURE_BUILD_TIME.')
    require_clean_worktree()
    restore_to = current_ref()

    journal_fd, args.journal = tempfile.mkstemp(prefix='bisect-build-time-journal-',
                                                suffix='.jsonl')
    os.close(journal_fd)
    harness_dir, harness_script = harness_copy(entry_script)
    args.build_dir = Path(tempfile.mkdtemp(prefix='WebKitBuild-bisect-',
                                           dir=args.source_dir))
    log.info('Building in %s, removed when the run ends, so the clean test never '
             'deletes your own build directory.', args.build_dir)
    if args.copy_measure_build_time:
        # Before anything is checked out, so the copy is the version launched with.
        copy = benchmark_copy(args.measure_build_time, harness_dir, args.source_dir)
        if copy is not None:
            args.measure_build_time = copy

    if args.cache:
        log.info('Profiling cache: %s (signature %s, entries expire after %s)',
                 args.cache, args.signature,
                 f'{args.cache_max_age:g}d' if args.cache_max_age else 'never')
    else:
        log.info('Profiling cache disabled; every commit will be built.')

    progress = Progress(expected_runs(args.good, args.bad, args.runs),
                        args.progress_enabled)
    awake = subprocess.Popen(('caffeinate', '-ims'))

    # `baseline_commit` stays live past an interrupt: the handler below falls through
    # to the summary, which needs it even when the baseline never finished.
    baseline_commit = None
    rc, first_bad, interrupted = 1, None, False
    try:
        # Calibration checks out the endpoints directly, so restore the original
        # ref however it ends — including the aborts inside prepare_baseline.
        try:
            baseline, baseline_commit = prepare_baseline(args, forwarded, progress,
                                                         hook, cache_context)
        finally:
            git('checkout', '--quiet', restore_to, check=False)

        harness = [
            sys.executable, str(harness_script),
            '--run-harness',
            *harness_args,
            '--test', args.test,
            '--runs', str(args.runs),
            '--journal', args.journal,
            # Resolved here: the copy can't find the benchmark relative to itself,
            # and every commit has to be built in the same throwaway directory.
            '--measure-build-time', str(args.measure_build_time),
            '--build-dir', str(args.build_dir),
        ]
        if args.progress_enabled:
            harness.append('--progress-ticks')
        if args.cache:
            # Absolute, so the harness never has to re-resolve it; its absence in
            # the harness argv is what turns caching off there.
            harness += ['--cache', str(args.cache),
                        '--cache-max-age', repr(args.cache_max_age)]
            if args.cache_tag:
                harness += ['--cache-tag', args.cache_tag]
            if args.refresh:
                harness.append('--refresh')
        else:
            harness.append('--no-cache')
        harness += ['--alpha', repr(args.alpha),
                    '--baseline', ','.join(repr(x) for x in baseline)]
        if forwarded:
            harness += ['--', *forwarded]

        log.info('Starting bisect: good=%s bad=%s runs=%d alpha=%s',
                 args.good, args.bad, args.runs, args.alpha)
        try:
            git('bisect', 'start')
            git('bisect', 'bad', args.bad)
            git('bisect', 'good', args.good)
            rc, first_bad = run_bisect(harness, progress)
            if rc != 0:
                log.error('git bisect run exited with code %d', rc)
        finally:
            log.info('Resetting bisect state (returning to %s).', restore_to)
            git('bisect', 'reset', check=False)
    except KeyboardInterrupt:
        # Abandoning a long bisect with Ctrl-C is a normal way to end one: the
        # `finally` blocks above have already put the checkout back, so report what
        # was measured and leave quietly instead of raising through the exit.
        interrupted = True
        log.warning('Interrupted by Ctrl-C.%s', ' Timings measured so far are in the '
                    'cache, so re-running will not rebuild them.' if args.cache else '')
    except SystemExit:
        # An abort mid-run — an endpoint that won't build, no regression in the range
        # — has nothing to summarize, so don't leave the journal behind either.
        Path(args.journal).unlink(missing_ok=True)
        raise
    finally:
        progress.close()
        awake.terminate()
        awake.wait()
        shutil.rmtree(harness_dir, ignore_errors=True)
        log.info('Removing the build directory %s.', args.build_dir)
        shutil.rmtree(args.build_dir, ignore_errors=True)

    print_summary(args.journal, first_bad, args.test, args.bad,
                  runs=args.runs, alpha=args.alpha,
                  baseline_commit=baseline_commit, cache=args.cache,
                  interrupted=interrupted)
    os.unlink(args.journal)
    return EXIT_INTERRUPTED if interrupted else rc


# --- Command line -----------------------------------------------------------

DESCRIPTION = 'Bisect a build-time regression using measure-build-time.'
EPILOG = ('Arguments after `--` are forwarded to measure-build-time '
          '(e.g. `-- --make`). Do not pass --tests, --output, or --keep-going '
          'there; this script sets them.')


def build_parser(description: str = DESCRIPTION,
                 epilog: str = EPILOG) -> argparse.ArgumentParser:
    """The full command line, shared by every front end onto this module."""
    parser = argparse.ArgumentParser(
        description=description, epilog=epilog,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument('--good', help='Known-fast commit (older).')
    parser.add_argument('--bad', help='Known-slow commit (newer).')
    parser.add_argument('--test', default='clean',
                        help='measure-build-time benchmark to bisect (default: clean).')
    parser.add_argument('-r', '--runs', type=int, default=3,
                        help='Timing runs per commit, and per baseline endpoint '
                             '(default: 3, minimum 2). More runs cost more builds but '
                             'give the t-test the power to resolve a smaller '
                             'regression.')
    parser.add_argument('--alpha', type=float, default=0.05,
                        help='t-test significance level (default: 0.05). A commit is '
                             '"bad" when it is significantly slower than the baseline.')
    parser.add_argument('--force', action='store_true',
                        help='Bisect even if calibration finds no regression.')
    parser.add_argument('--measure-build-time', type=Path, default=None,
                        help='Path to the measure-build-time benchmark (default: '
                             '$MEASURE_BUILD_TIME, else the copy beside this script).')
    parser.add_argument('--copy-measure-build-time',
                        action=argparse.BooleanOptionalAction, default=True,
                        help='Measure every commit with a copy of measure-build-time '
                             'taken from this checkout before bisecting and run from '
                             'outside it, so a run started from a tree that changes '
                             'the benchmark keeps those changes.')
    cache = parser.add_argument_group(
        'profiling cache',
        'Timings are cached per commit so re-running a bisect — after widening the '
        'range, say — only builds commits it has not measured. Reuse is scoped to '
        'the benchmark, the forwarded arguments and this machine.')
    cache.add_argument('--cache', default=None,
                       help='Cache file (default: webkit-build-time-cache.jsonl in '
                            'this checkout\'s git directory).')
    cache.add_argument('--no-cache', action='store_true',
                       help='Build every commit, reusing nothing (wins over --cache).')
    cache.add_argument('--refresh', action='store_true',
                       help='Ignore cached timings, but still measure and record new '
                            'ones — use after the machine or toolchain changed.')
    cache.add_argument('--cache-max-age', type=float, default=7,
                       metavar='DAYS',
                       help='Ignore cached timings older than this (default: 7; 0 '
                            'never expires). Absolute build times drift.')
    cache.add_argument('--cache-tag', default=None,
                       help='Extra string mixed into the cache signature, to keep '
                            'measurements from different conditions apart.')
    cache.add_argument('--show-cache', action='store_true',
                       help='Print the cached timings matching this run\'s signature '
                            'and exit.')
    parser.add_argument('--progress', action=argparse.BooleanOptionalAction, default=None,
                        help='Show a tqdm progress bar over the expected number of '
                             'timing runs (default: on when attached to a terminal '
                             'and tqdm is installed).')
    parser.add_argument('--run-harness', action='store_true',
                        help=argparse.SUPPRESS)  # internal: per-commit test
    parser.add_argument('--journal', default=None,
                        help=argparse.SUPPRESS)  # internal: per-commit timing log
    parser.add_argument('--build-dir', type=Path, default=None,
                        help=argparse.SUPPRESS)  # internal: the run's build directory
    parser.add_argument('--baseline', default=None,
                        help=argparse.SUPPRESS)  # internal: baseline samples (csv)
    parser.add_argument('--progress-ticks', action='store_true',
                        help=argparse.SUPPRESS)  # internal: emit per-run tick sentinels
    return parser


def split_forwarded(argv: list[str]) -> tuple[list[str], list[str]]:
    """Split `argv` at the first `--` into (our arguments, forwarded arguments)."""
    if '--' not in argv:
        return argv, []
    split = argv.index('--')
    return argv[:split], argv[split + 1:]


def resolve_arguments(parser: argparse.ArgumentParser, args: argparse.Namespace,
                      forwarded: list[str], *, benchmark: Path | None = None,
                      needs_endpoints: bool = True) -> None:
    """Apply defaults that depend on other arguments, and validate combinations.

    `benchmark` is where to find measure-build-time when neither the flag nor
    $MEASURE_BUILD_TIME says (the caller knows its own layout). `forwarded` is part
    of the cache signature, since it decides what is actually built.
    """
    # Two samples is the least a variance — and so a t-test — can be computed from.
    if args.runs < 2:
        parser.error('--runs must be >= 2: a commit is classified by comparing the '
                     'spread of its timings against the baseline\'s, which a single '
                     'measurement cannot give.')

    if args.measure_build_time is None:
        env = os.environ.get('MEASURE_BUILD_TIME')
        args.measure_build_time = Path(env) if env else benchmark
    # Both the driver and the harness run inside the checkout being bisected, so this
    # is the source directory to build; the throwaway build directory is the driver's
    # to create and the harness's to be told.
    args.source_dir = repo_root()

    # Resolve the cache: the driver finds it from the repo, and hands the harness an
    # absolute path (or nothing at all, which is how caching stays off there).
    args.signature = cache_signature(args.test, forwarded, args.cache_tag)
    if args.no_cache:
        args.cache = None
    elif args.cache:
        args.cache = Path(args.cache).resolve()
    elif not args.run_harness:
        args.cache = default_cache_path()
    else:
        args.cache = None

    if args.run_harness:
        if args.baseline is None:
            parser.error('--run-harness requires --baseline')
        if args.measure_build_time is None:
            parser.error('--run-harness requires --measure-build-time')
        if args.build_dir is None:
            parser.error('--run-harness requires --build-dir')
    else:
        if needs_endpoints and (not args.good or not args.bad):
            parser.error('--good and --bad are required')
        if args.measure_build_time is None:
            parser.error('Could not find measure-build-time; pass '
                         '--measure-build-time or set $MEASURE_BUILD_TIME')
    # Resolve the progress bar: explicit flag wins, else auto-enable on a TTY.
    args.progress_enabled = (args.progress if args.progress is not None
                             else (tqdm is not None and sys.stderr.isatty()))
    if args.progress_enabled and tqdm is None:
        log.warning('tqdm not installed; progress bar disabled '
                    '(`pip3 install tqdm` to enable).')
        args.progress_enabled = False


def configure_logging() -> None:
    logging.basicConfig(level=logging.INFO, format=LOG_FORMAT)


def interrupted_exit() -> int:
    """Log a Ctrl-C and return its exit code, for a front end to `sys.exit`.

    Everything this tool has to undo — the bisect state, the checkouts — is undone
    in `finally` blocks, which run whether or not the interrupt is caught, so the
    traceback Python would print on the way out is noise. Front ends call this
    instead of letting KeyboardInterrupt escape.
    """
    log.warning('Interrupted.')
    return EXIT_INTERRUPTED


def main(entry_script: Path, argv: list[str] | None = None, *, hook=None,
         harness_args=(), benchmark: Path | None = None, cache_context=None) -> int:
    """Entry point for a front end that adds nothing to the shared command line."""
    configure_logging()
    argv, forwarded = split_forwarded(sys.argv[1:] if argv is None else argv)
    parser = build_parser()
    args = parser.parse_args(argv)
    resolve_arguments(parser, args, forwarded,
                      benchmark=benchmark or entry_script.parent / 'measure-build-time',
                      needs_endpoints=not args.show_cache)
    if args.show_cache:
        return show_cache(args)
    try:
        if args.run_harness:
            return run_harness(args, forwarded, hook=hook, cache_context=cache_context)
        return run_driver(args, forwarded, entry_script=entry_script, hook=hook,
                          harness_args=harness_args, cache_context=cache_context)
    except KeyboardInterrupt:
        # A backstop: the driver handles the first Ctrl-C itself and returns, so this
        # catches one pressed during that cleanup, or one in harness mode.
        return interrupted_exit()
