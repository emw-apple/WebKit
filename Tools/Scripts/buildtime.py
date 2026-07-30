"""Shared implementation of the `bisect-build-time` build-time bisection tool.

Given a commit range and a `measure-build-time` benchmark (default: `clean`), this
drives `git bisect` to find the commit that introduced a build-time regression.

By default each commit is timed several times (`--runs`, default 3) and compared to
a baseline (the good endpoint, also timed `--runs` times) with a two-sample Welch's
t-test: the first commit that is *significantly slower* than the baseline
(one-sided, `p <= --alpha`) is the regression point. This is robust to the
run-to-run variability that makes a single measurement unreliable. For a cheaper
single-measurement run, pass `--runs 1` (or a `--threshold`): a commit is then "bad"
if its build time is at or above `--threshold` seconds (auto-calibrated from the
endpoints' midpoint when omitted).

`Tools/Scripts/bisect-build-time` is a thin entry point onto `main()` here. Callers
that need to prepare something before each commit is timed — a companion checkout,
say — build the parser with `build_parser()` and call `run_driver`/`run_harness`
with a `hook` callable:

    hook(commit_sha, writer) -> bool     # False: this commit cannot be measured

Everything here is stdlib-only apart from an optional `tqdm` progress bar.
"""

from __future__ import annotations

import argparse
import json
import logging
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

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

    def tick(self, n: int = 1) -> None:
        if self.bar is not None:
            self.bar.update(n)

    def write(self, text: str) -> None:
        if self.bar is not None:
            tqdm.write(text, end='', file=sys.stdout)
        else:
            sys.stdout.write(text)
            sys.stdout.flush()

    def close(self) -> None:
        if self.bar is not None:
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
                  writer=None) -> float | None:
    """Run `measure-build-time` for one benchmark; return its wall seconds.

    Returns None if the build fails (so the caller can skip the commit).
    """
    if writer is None:
        writer = stdout_writer
    with tempfile.NamedTemporaryFile(prefix='bisect-build-time-', suffix='.json') as out:
        command = [
            str(benchmark),
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


def run_samples(benchmark: Path, test_name: str, forwarded: list[str], runs: int,
                on_sample=None, writer=None) -> list[float] | None:
    """Run the benchmark `runs` times; return the wall times (None if any fails).

    `on_sample` (if given) is called once after each run attempt — used to
    advance the progress bar (driver) or emit a tick sentinel (harness).
    `writer` is forwarded to `run_benchmark` to route build output through the
    progress bar.
    """
    samples: list[float] = []
    for i in range(1, runs + 1):
        if runs > 1:
            log.info('Timing run %d/%d', i, runs)
        seconds = run_benchmark(benchmark, test_name, forwarded, writer=writer)
        if on_sample is not None:
            on_sample()
        if seconds is None:
            return None
        samples.append(seconds)
    return samples


# --- Statistics (stdlib only) -----------------------------------------------

def _betacf(a: float, b: float, x: float) -> float:
    """Continued-fraction expansion for the incomplete beta function (NR §6.4)."""
    MAXIT, EPS, FPMIN = 200, 3.0e-12, 1.0e-300
    qab, qap, qam = a + b, a + 1.0, a - 1.0
    c = 1.0
    d = 1.0 - qab * x / qap
    if abs(d) < FPMIN:
        d = FPMIN
    d = 1.0 / d
    h = d
    for m in range(1, MAXIT + 1):
        m2 = 2 * m
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1.0 + aa * d
        if abs(d) < FPMIN:
            d = FPMIN
        c = 1.0 + aa / c
        if abs(c) < FPMIN:
            c = FPMIN
        d = 1.0 / d
        h *= d * c
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1.0 + aa * d
        if abs(d) < FPMIN:
            d = FPMIN
        c = 1.0 + aa / c
        if abs(c) < FPMIN:
            c = FPMIN
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) < EPS:
            break
    return h


def betai(a: float, b: float, x: float) -> float:
    """Regularized incomplete beta function I_x(a, b)."""
    if x <= 0.0:
        return 0.0
    if x >= 1.0:
        return 1.0
    lbeta = (math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b)
             + a * math.log(x) + b * math.log(1.0 - x))
    front = math.exp(lbeta)
    if x < (a + 1.0) / (a + b + 2.0):
        return front * _betacf(a, b, x) / a
    return 1.0 - front * _betacf(b, a, 1.0 - x) / b


def welch_ttest(a: list[float], b: list[float]) -> tuple[float, float]:
    """Two-sample Welch's t-test comparing `a` against `b` (each len >= 2).

    Returns (t, p_two_sided); t > 0 means mean(a) > mean(b).
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
    p = betai(df / 2.0, 0.5, df / (df + t * t))
    return t, p


def format_pvalue(p: float | None) -> str:
    """Compact p-value: scientific for tiny values, 3 decimals otherwise."""
    if p is None:
        return '—'
    return f'{p:.1e}' if p < 1e-3 else f'{p:.3f}'


def record_measurement(journal: str | None, commit: str,
                       samples: list[float] | None, pvalue: float | None = None) -> None:
    """Append one commit's measured wall times to the shared journal (JSON lines).

    The per-commit harness runs in its own process, so it can't return timings
    to the driver directly; each invocation logs here and the driver reads the
    journal back to print the final summary. `samples` is None for a skipped
    (build-failed) commit; `pvalue` is the t-test result vs baseline (t-test mode)
    or None (threshold mode / the baseline itself). Raw samples are preserved so
    verdicts can be recomputed at summary time.
    """
    if not journal:
        return
    with open(journal, 'a') as f:
        f.write(json.dumps({'commit': commit, 'samples': samples, 'pvalue': pvalue}) + '\n')


# --- Harness mode (invoked once per commit by `git bisect run`) -------------

def run_harness(args: argparse.Namespace, forwarded: list[str], *, hook=None) -> int:
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
    on_sample = (lambda: print(TICK, flush=True)) if args.progress_ticks else None
    samples = run_samples(args.measure_build_time, args.test, forwarded, args.runs,
                          on_sample=on_sample)
    if samples is None:
        record_measurement(args.journal, commit, None, None)
        log.warning('Skipping this commit (build failed or no timing).')
        return EXIT_SKIP

    mean = statistics.mean(samples)
    pvalue = None
    if args.baseline is not None:
        # t-test mode: significantly slower than the baseline (one-sided) is bad.
        baseline = [float(x) for x in args.baseline.split(',') if x]
        t, pvalue = welch_ttest(samples, baseline)
        is_bad = t > 0 and pvalue <= args.alpha
        log.info('%s: mean %.1fs over %d runs, p=%s vs baseline -> %s',
                 args.test, mean, args.runs, format_pvalue(pvalue),
                 'BAD' if is_bad else 'GOOD')
    else:
        # threshold mode: at or above the threshold is bad.
        is_bad = mean >= args.threshold
        log.info('%s: %.1fs (threshold %.1fs) -> %s',
                 args.test, mean, args.threshold, 'BAD' if is_bad else 'GOOD')

    record_measurement(args.journal, commit, samples, pvalue)
    return EXIT_BAD if is_bad else EXIT_GOOD


# --- Driver mode ------------------------------------------------------------

def checkout_for_measurement(ref: str, hook, writer) -> str:
    """Check out `ref` for measurement and return the commit SHA it resolved to.

    Runs the checkout hook (if any) so anything the build depends on is prepared
    before the benchmark runs. Exits on hook failure: unlike a bisect step, an
    endpoint we cannot prepare cannot simply be skipped.
    """
    git('checkout', '--quiet', ref)
    sha = git_output('rev-parse', 'HEAD')
    if hook is not None and not hook(sha, writer):
        sys.exit(f'Checkout hook failed for {ref} ({sha[:12]}); cannot measure '
                 f'this endpoint.')
    return sha


def calibrate(args: argparse.Namespace, forwarded: list[str], progress: Progress,
              hook=None) -> float:
    """Measure the bad and good endpoints once; return the midpoint threshold.

    The caller restores the original ref; this leaves HEAD on the good endpoint.
    """
    log.info('Calibrating threshold by measuring both endpoints...')
    bad_sha = checkout_for_measurement(args.bad, hook, progress.write)
    t_bad = run_benchmark(args.measure_build_time, args.test, forwarded,
                          writer=progress.write)
    progress.tick()
    record_measurement(args.journal, bad_sha, [t_bad] if t_bad is not None else None)
    good_sha = checkout_for_measurement(args.good, hook, progress.write)
    t_good = run_benchmark(args.measure_build_time, args.test, forwarded,
                           writer=progress.write)
    progress.tick()
    record_measurement(args.journal, good_sha, [t_good] if t_good is not None else None)

    if t_bad is None or t_good is None:
        sys.exit('Calibration failed: an endpoint build did not produce a timing. '
                 'Fix the build or pass --threshold explicitly.')
    log.info('Endpoints: good=%.1fs bad=%.1fs', t_good, t_bad)
    if t_bad <= t_good and not args.force:
        sys.exit(f'Bad endpoint ({t_bad:.1f}s) is not slower than good '
                 f'({t_good:.1f}s); no regression detected in range. Pass an '
                 f'explicit --threshold, or --force to bisect anyway.')
    threshold = (t_good + t_bad) / 2
    log.info('Using midpoint threshold: %.1fs', threshold)
    return threshold


def prepare_baseline(args: argparse.Namespace, forwarded: list[str],
                     progress: Progress, hook=None) -> tuple[list[float], str]:
    """Measure both endpoints `--runs` times; return (baseline_samples, good_sha).

    The good endpoint is the baseline. The bad endpoint is measured too, as a
    sanity check that the range actually contains a detectable regression. The
    caller restores the original ref; this leaves HEAD on the bad endpoint.
    """
    log.info('Measuring baseline over %d runs at each endpoint (t-test mode)...',
             args.runs)
    good_sha = checkout_for_measurement(args.good, hook, progress.write)
    baseline = run_samples(args.measure_build_time, args.test, forwarded, args.runs,
                           on_sample=progress.tick, writer=progress.write)
    if baseline is not None:
        record_measurement(args.journal, good_sha, baseline, None)
    bad_sha = checkout_for_measurement(args.bad, hook, progress.write)
    bad_samples = run_samples(args.measure_build_time, args.test, forwarded, args.runs,
                              on_sample=progress.tick, writer=progress.write)

    if baseline is None or bad_samples is None:
        sys.exit('Calibration failed: an endpoint build did not produce timings. '
                 'Fix the build or reduce the range.')
    t, p = welch_ttest(bad_samples, baseline)
    record_measurement(args.journal, bad_sha, bad_samples, p)
    log.info('Baseline good=%.1fs bad=%.1fs (p=%s)',
             statistics.mean(baseline), statistics.mean(bad_samples), format_pvalue(p))
    if not (t > 0 and p <= args.alpha) and not args.force:
        sys.exit(f'Bad endpoint is not significantly slower than good '
                 f'(p={format_pvalue(p)} at alpha={args.alpha}); no detectable '
                 f'regression in range. Increase --runs, widen the range, or pass '
                 f'--force to bisect anyway.')
    return baseline, good_sha


FIRST_BAD_RE = re.compile(r'^([0-9a-f]{7,40}) is the first bad commit', re.MULTILINE)


def expected_runs(good: str, bad: str, runs: int, calibrating: bool) -> int:
    """Estimate total measure-build-time invocations for the progress bar.

    git bisect tests ~log2(range) commits, each timed `runs` times, plus two
    calibration endpoints (also `runs` each) when calibrating. Approximate —
    skips and uneven splits vary the real count; tqdm tolerates over/undershoot.
    """
    try:
        n = int(git_output('rev-list', '--count', f'{good}..{bad}'))
    except (ValueError, subprocess.CalledProcessError):
        n = 0
    steps = math.ceil(math.log2(n)) if n >= 1 else 0
    return runs * steps + (2 * runs if calibrating else 0)


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
                  runs: int, threshold: float | None, alpha: float,
                  baseline_commit: str | None) -> None:
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

    ttest = threshold is None
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
        if not samples:
            nstr, meanstr, pstr, verdict = '0', 'skip', '—', 'skip'
        else:
            mean = statistics.mean(samples)
            nstr, meanstr = str(len(samples)), f'{mean:.1f}s'
            if ttest and commit == baseline_full:
                pstr, verdict = 'base', 'base'
            elif ttest:
                pstr = format_pvalue(pvalue)
                verdict = ('bad' if (pvalue is not None and pvalue <= alpha
                                     and baseline_mean is not None and mean > baseline_mean)
                           else 'good')
            else:
                pstr = '—'
                verdict = 'bad' if mean >= threshold else 'good'
        rows.append((rank.get(commit, -1), short, commit, date, nstr, meanstr, pstr,
                     verdict, subject))
    rows.sort(reverse=True)  # oldest (highest rank) first

    sha_w = max(len('COMMIT'), *(len(r[1]) for r in rows))
    mean_w = max(len('MEAN'), *(len(r[5]) for r in rows))
    p_w = max(len('P-VALUE'), *(len(r[6]) for r in rows))
    verdict_w = max(len('VERDICT'), *(len(r[7]) for r in rows))

    bold, red, reset = ('\033[1m', '\033[31m', '\033[0m') if sys.stdout.isatty() else ('', '', '')

    print()
    if ttest:
        print(f'Build-time bisect summary (test: {test_name}, runs: {runs}, '
              f'alpha: {alpha})')
    else:
        print(f'Build-time bisect summary (test: {test_name}, threshold: {threshold:.1f}s)')
    print()
    print(f'    {"COMMIT":<{sha_w}}  {"DATE":<10}  {"RUNS":>4}  {"MEAN":>{mean_w}}  '
          f'{"P-VALUE":>{p_w}}  {"VERDICT":<{verdict_w}}  SUBJECT')
    for _rank, short, commit, date, nstr, meanstr, pstr, verdict, subject in rows:
        is_first_bad = bool(first_bad_full) and commit == first_bad_full
        marker = '>>> ' if is_first_bad else '    '
        subj = subject if len(subject) <= 60 else subject[:57] + '...'
        line = (f'{marker}{short:<{sha_w}}  {date:<10}  {nstr:>4}  {meanstr:>{mean_w}}  '
                f'{pstr:>{p_w}}  {verdict:<{verdict_w}}  {subj}')
        if is_first_bad:
            line = f'{bold}{red}{line}{reset}  <- first bad commit'
        print(line)
    print()

    if ttest and baseline_mean is not None:
        base_short = next((r[1] for r in rows if r[2] == baseline_full), baseline_full[:9])
        print(f'Baseline: {base_short} (good endpoint), '
              f'{baseline_mean:.1f}s mean over {runs} runs')
    first_row = next((r for r in rows if r[2] == first_bad_full), None)
    if first_row:
        print(f'First bad commit: {first_row[1]} {first_row[8]}')
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


def run_driver(args: argparse.Namespace, forwarded: list[str], *, entry_script: Path,
               hook=None, harness_args=()) -> int:
    """Calibrate the endpoints, then drive `git bisect run` over the range.

    `entry_script` is re-invoked once per commit in harness mode (from a temp copy).
    `hook` prepares each commit before it is timed; `harness_args` are extra
    arguments `entry_script` needs to reconstruct itself in the harness process.
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

    ttest = args.runs > 1
    calibrating = ttest or args.threshold is None
    progress = Progress(expected_runs(args.good, args.bad, args.runs, calibrating),
                        args.progress_enabled)

    baseline_commit = None
    try:
        # Calibration checks out the endpoints directly, so restore the original
        # ref however it ends — including the aborts inside these functions.
        try:
            if ttest:
                baseline, baseline_commit = prepare_baseline(args, forwarded, progress,
                                                             hook)
            elif args.threshold is None:
                args.threshold = calibrate(args, forwarded, progress, hook)
        finally:
            git('checkout', '--quiet', restore_to, check=False)

        harness = [
            sys.executable, str(harness_script),
            '--run-harness',
            *harness_args,
            '--test', args.test,
            '--runs', str(args.runs),
            '--journal', args.journal,
            # Resolved here: the copy can't find the benchmark relative to itself.
            '--measure-build-time', str(args.measure_build_time),
        ]
        if args.progress_enabled:
            harness.append('--progress-ticks')
        if ttest:
            harness += ['--alpha', repr(args.alpha),
                        '--baseline', ','.join(repr(x) for x in baseline)]
        else:
            harness += ['--threshold', repr(args.threshold)]
        if forwarded:
            harness += ['--', *forwarded]

        if ttest:
            log.info('Starting bisect: good=%s bad=%s runs=%d alpha=%s',
                     args.good, args.bad, args.runs, args.alpha)
        else:
            log.info('Starting bisect: good=%s bad=%s threshold=%.1fs',
                     args.good, args.bad, args.threshold)
        rc, first_bad = 1, None
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
    finally:
        progress.close()
        shutil.rmtree(harness_dir, ignore_errors=True)

    print_summary(args.journal, first_bad, args.test, args.bad,
                  runs=args.runs, threshold=None if ttest else args.threshold,
                  alpha=args.alpha, baseline_commit=baseline_commit)
    os.unlink(args.journal)
    return rc


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
    parser.add_argument('-r', '--runs', type=int, default=None,
                        help='Timing runs per commit (and per calibration endpoint). '
                             '>1 (default: 3) uses a two-sample t-test against the '
                             'good-endpoint baseline; 1 uses single-measurement '
                             '--threshold classification.')
    parser.add_argument('--alpha', type=float, default=0.05,
                        help='t-test significance level (default: 0.05). A commit is '
                             '"bad" when it is significantly slower than the baseline.')
    parser.add_argument('--threshold', type=float, default=None,
                        help='Single-run mode: build time in seconds at or above which '
                             'a commit is "bad" (omit to auto-calibrate the midpoint). '
                             'Implies --runs 1; incompatible with --runs > 1.')
    parser.add_argument('--force', action='store_true',
                        help='Bisect even if calibration finds no regression.')
    parser.add_argument('--measure-build-time', type=Path, default=None,
                        help='Path to the measure-build-time benchmark (default: '
                             '$MEASURE_BUILD_TIME, else the copy beside this script).')
    parser.add_argument('--progress', action=argparse.BooleanOptionalAction, default=None,
                        help='Show a tqdm progress bar over the expected number of '
                             'timing runs (default: on when attached to a terminal '
                             'and tqdm is installed).')
    parser.add_argument('--run-harness', action='store_true',
                        help=argparse.SUPPRESS)  # internal: per-commit test
    parser.add_argument('--journal', default=None,
                        help=argparse.SUPPRESS)  # internal: per-commit timing log
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


def resolve_arguments(parser: argparse.ArgumentParser, args: argparse.Namespace, *,
                      benchmark: Path | None = None,
                      needs_endpoints: bool = True) -> None:
    """Apply defaults that depend on other arguments, and validate combinations.

    `benchmark` is where to find measure-build-time when neither the flag nor
    $MEASURE_BUILD_TIME says (the caller knows its own layout).
    """
    # Resolve --runs: default to single-run when a --threshold is given, else 3.
    if args.runs is None:
        args.runs = 1 if args.threshold is not None else 3
    if args.runs < 1:
        parser.error('--runs must be >= 1')

    if args.measure_build_time is None:
        env = os.environ.get('MEASURE_BUILD_TIME')
        args.measure_build_time = Path(env) if env else benchmark

    if args.run_harness:
        if args.baseline is None and args.threshold is None:
            parser.error('--run-harness requires --threshold or --baseline')
        if args.measure_build_time is None:
            parser.error('--run-harness requires --measure-build-time')
    else:
        if args.runs > 1 and args.threshold is not None:
            parser.error('--threshold is single-run classification; it is incompatible '
                         'with --runs > 1 (t-test mode)')
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


def main(entry_script: Path, argv: list[str] | None = None, *, hook=None,
         harness_args=(), benchmark: Path | None = None) -> int:
    """Entry point for a front end that adds nothing to the shared command line."""
    configure_logging()
    argv, forwarded = split_forwarded(sys.argv[1:] if argv is None else argv)
    parser = build_parser()
    args = parser.parse_args(argv)
    resolve_arguments(parser, args,
                      benchmark=benchmark or entry_script.parent / 'measure-build-time')
    if args.run_harness:
        return run_harness(args, forwarded, hook=hook)
    return run_driver(args, forwarded, entry_script=entry_script, hook=hook,
                      harness_args=harness_args)
