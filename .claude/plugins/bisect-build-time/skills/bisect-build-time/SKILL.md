---
name: bisect-build-time
description: Use when the user wants to find which commit in a range caused a WebKit build-time regression (clean or incremental build getting slower). Drives `git bisect run` on top of `Tools/Scripts/measure-build-time` via `Tools/Scripts/bisect-build-time`, timing each commit several times and using a two-sample t-test (default) to find the first commit significantly slower than the baseline; a single-measurement `--threshold` mode is also available. Timings are cached per commit, so re-running over an overlapping range is cheap. When `../Internal/Tools/Scripts/bisect-build-time` exists, run that instead — same tool and command line, plus it keeps the sibling checkout aligned with each commit under test.
user-invocable: true
allowed-tools: Bash, Read
---

## When to use

The user has observed that a `measure-build-time` benchmark (a clean build, or
an incremental rebuild after touching a hot header) got slower somewhere in a
known commit range, and wants the culprit commit. `Tools/Scripts/bisect-build-time`
automates `git bisect run`: it checks out commits, times the benchmark, and
classifies each as fast (good) or slow (bad).

**First check for a sibling wrapper.** If
`../Internal/Tools/Scripts/bisect-build-time` exists, invoke that instead of this
script — see "Sibling checkouts" below. It is the same tool with the same command
line; running this script directly on such a checkout measures mismatched pairs.

By default it times each commit several times and uses a **two-sample t-test**
against a baseline (see below), which is robust to the run-to-run variability
that otherwise flags innocent commits as the regression. For large, obvious
regressions where speed matters more than robustness, a cheaper single-run
`--threshold` mode is available (`--runs 1`).

## Prerequisites

- A **clean working tree** for tracked files (`git bisect` requires it).
  Untracked files are fine. Commit or stash local changes first.
- A build backend to forward to `measure-build-time`, usually `--make` (Xcode)
  or `--cmake` (Ninja). See `Tools/Scripts/measure-build-time --help` for the
  full set (`--build-command`, `--configuration`, etc.).
- Known-good (older, fast) and known-bad (newer, slow) commits bounding the
  regression. The good endpoint must build (it is the t-test baseline).
- On a sibling checkout, `GIT_LFS_SKIP_SMUDGE=1` unless the build needs the LFS
  payloads — see "Git LFS in the sibling checkout" below.

## Basic usage

```sh
Tools/Scripts/bisect-build-time --good <old-sha> --bad <new-sha> -- --make
```

Everything after `--` is forwarded verbatim to `measure-build-time`. Do **not**
pass `--tests`, `--output`, or `--keep-going` there — the script sets them.

- `--test <name>` picks the benchmark (default `clean`). For incremental
  benchmarks (`webcore-header`, `jsc-cpp-source`, `serialization-file`, … — see
  `measure-build-time --help`), the script automatically runs `clean` first
  since they depend on it, and measures only the incremental rebuild.
- `-r/--runs <N>` (**default 3**): timings per commit *and* per calibration
  endpoint. `N > 1` selects **t-test mode**; `N = 1` selects single-run
  `--threshold` mode.
- `--alpha <p>` (default `0.05`): t-test significance level.
- `--threshold <seconds>`: single-run mode — a commit whose build time is
  **≥ threshold** is bad. Passing `--threshold` implies `--runs 1`; omit it (with
  `--runs 1`) to auto-calibrate the endpoints' midpoint. Incompatible with
  `--runs > 1`.
- `--force`: bisect even if calibration finds no detectable regression.
- `--progress` / `--no-progress`: a tqdm bar over the expected number of timing
  runs. Auto-enabled when attached to a terminal and `tqdm` is installed;
  harmless no-op (with a one-line warning if explicitly requested) otherwise.

## Sibling checkouts

`measure-build-time --make` builds `make -C ../Internal/WebKit` whenever that
directory exists, so on such a checkout every timing compiles WebKit *through* the
sibling repo's configuration files and workspace. Bisecting WebKit alone leaves
that checkout pinned wherever it started while `git bisect` walks WebKit back days
or weeks: builds fail (every commit skips) or, worse, the times reflect a
mismatched pair rather than the commit under test.

So when `../Internal/Tools/Scripts/bisect-build-time` exists, run it instead:

```sh
../Internal/Tools/Scripts/bisect-build-time --good <old-sha> --bad <new-sha> -- --make
```

It is a front end onto the same implementation (`Tools/Scripts/buildtime.py`) and
accepts every flag above, so `--help` there lists the whole command line. Before
each WebKit commit is timed it checks out the sibling commit whose committer date is
newest at or before it (nothing records a real correspondence between the two
repos, so they are paired by landing time). Extra flags it adds:

- `--dry-run`: print the commit each endpoint pairs with and stop. A few git calls
  — always worth running before a multi-hour bisect.
- `--internal-ref <ref>`: ref whose history is searched (default `origin/main`,
  falling back to the current branch). Remote-tracking by default so a stale local
  branch can't quietly pin the whole range.
- `--align-internal [<commit>]`: check out the sibling commit paired with one
  WebKit commit (default: HEAD) and exit — useful on its own to reproduce a build
  at an older commit. Add `--dry-run` to print the pairing without checking
  anything out.
- `--internal-dir` / `--opensource-dir`: the two checkouts, defaulting to the
  wrapper's own repo and its `OpenSource` sibling.

Extra prerequisites: **both** checkouts need clean tracked files (the sibling is
checked out repeatedly), and a recent `git -C ../Internal fetch` so the search ref
covers the range. It aborts before any build if either is a problem, and refuses to
run when both endpoints pair with the same sibling commit, since aligning would
then do nothing across the range (`--force` overrides). A WebKit commit predating
all of the sibling's history becomes a `skip` mid-range, or a pre-flight abort as an
endpoint. Both checkouts are restored when the run ends, including after Ctrl-C.

Two things that pass the pre-flight check and still break every alignment checkout:

- **Untracked files in the sibling that are tracked in the range.** "Untracked files
  are fine" above applies to the WebKit checkout; a file merely untracked *now* in
  `Internal` but committed in the paired commits stops `git checkout` cold (`error:
  The following untracked working tree files would be overwritten by checkout`).
  Move or delete it first.
- **Git LFS** — see below.

### Git LFS in the sibling checkout

`Internal` tracks some files with Git LFS (`*.profdata.compressed`,
`*.partial.sdkdb`, …); `OpenSource` tracks none. So every
alignment checkout runs the LFS smudge filter, which fetches whatever the local LFS
cache is missing over SSH. When that host is unreachable — offline, no SSH
agent, or a sandbox blocking port 22 — the checkout fails and takes the bisect
with it:

```
Error downloading object: WebKit/WebKitAdditions/Profiling/.../JavaScriptCore.profdata.compressed
  ssh: connect to host <redacted> port 22: Operation not permitted
fatal: smudge filter lfs failed
ERROR Could not check out 772723fb0e89 in /Users/emw/src/Internal.
Checkout hook failed for ca48ed48ca69; cannot measure this endpoint.
```

An endpoint failing this way aborts calibration outright; a mid-range commit becomes a
`skip`. Export `GIT_LFS_SKIP_SMUDGE=1` for the whole run so checkouts write LFS pointer
files instead of fetching payloads — the hook shells out to `git checkout`, so it
inherits the environment:

```sh
GIT_LFS_SKIP_SMUDGE=1 ../Internal/Tools/Scripts/bisect-build-time --good <old> --bad <new> -- --make
```

Only do this when the build does not read those payloads. For a build-time
measurement the PGO profiles are the ones that matter. PGO is only used from
Release and Production builds, and by default, `measure-build-time` performs
Debug builds. So they are usually fine to omit. But when explicitly testing a
release configuration, skipping the smudge measures a different build than the
one you care about: pre-fetch instead, with `git -C ../Internal lfs pull` (or
`lfs fetch --recent`) while the network is available.

## Reusing measurements

Timings are cached, so re-running a bisect — after widening the range, or with a
different `--alpha` — only builds commits it hasn't measured. Cache hits are logged
as they happen, the summary's `CACHED` column shows how many of each commit's
samples were reused, and a `Cache: reused N of M samples` line names the file, so a
suspiciously fast run is always visible.

A cached timing is reused only for the same commit, benchmark (`--test`), forwarded
`measure-build-time` arguments, **and host** — reusing a fast machine's number on a
slow one would misclassify and send the bisect down the wrong branch. On an internal
checkout the paired sibling commit is part of the key too, so a time is never reused
across pairs. Cached samples *accumulate*: with `--runs 3`, a commit with 2 cached
samples gets one fresh one, and a commit with 5 keeps all 5 (Welch's t-test handles
unequal sample sizes).

The hazard is comparing cached numbers against fresh ones after conditions changed
— a toolchain upgrade, a different ccache state, a machine under load. Guards:

- Entries older than `--cache-max-age` days (default 7) are ignored; `0` never
  expires.
- When a commit's cached and fresh samples disagree by more than 10%, it warns.
- `--refresh` ignores what's cached and re-measures (use after upgrading Xcode);
  `--no-cache` bypasses the cache entirely; `--cache-tag <str>` keeps measurements
  from different conditions apart.
- `--show-cache` prints what's stored for the current signature; `--cache <path>`
  moves the file, which by default is `webkit-build-time-cache.jsonl` in the
  checkout's git directory (per worktree, and safe from the `clean` test).

## What it does per commit

**t-test mode (default).** Up front, the good and bad endpoints are each timed
`--runs` times; the **good endpoint is the baseline**, and the bad endpoint is a
sanity check (the run aborts unless bad is significantly slower than good, unless
`--force`). Then for each commit `git bisect` selects, it is timed `--runs`
times and compared to the baseline with a one-sided two-sample Welch's t-test:

- significantly **slower** than baseline (`p ≤ alpha`) → **bad** (exit 1)
- not significantly slower → **good** (exit 0)
- **build failed to compile** → **skip** (exit 125)

The first bad commit is the first one significantly slower than the baseline.

**Single-run mode (`--runs 1`).** One timing per commit; bad iff build time
**≥ threshold** (explicit, or the auto-calibrated good/bad midpoint).

## Cost

Each timing is a full benchmark run (a clean build is many minutes), and each
step runs the benchmark `--runs` times. A range of N commits costs roughly
**runs × (log2(N) + 2)** builds in t-test mode (the `+2` is endpoint
calibration), or `log2(N) + 2` in single-run mode (`+0` with an explicit
`--threshold`). Warn the user before kicking off a long range, and prefer running
it in the background. Commits already in the profiling cache are free (see
"Reusing measurements"), so a re-run over an overlapping range costs much less
than the formula suggests.

## Extending it: per-commit setup

When the build depends on a second checkout that must track the commit under test,
drive the bisect from Python instead of the command line: `import buildtime`, build
the command line with `build_parser()`, and pass a `hook` callable to
`run_driver`/`run_harness`:

```python
def hook(commit_sha, writer=None) -> bool:
    ...  # prepare the other checkout for this commit
```

The hook runs after each commit is checked out — the two calibration endpoints and
every commit `git bisect` selects — and the timing runs wait for it.

- Returns True: proceed with the timing runs.
- Returns False: the commit is skipped (exit 125) while bisecting, and calibration
  aborts with an error.

The hook is *not* run when the original ref is restored at the end, so a wrapper
that moves another checkout is responsible for restoring it too. Pass
`harness_args=[...]` to `run_driver` for any flags the wrapper needs to rebuild its
hook in the per-commit harness process. The sibling wrapper above is built this way.

## Output and cleanup

The script prints git's `… is the first bad commit` result, then always runs
`git bisect reset` (even on error or Ctrl-C) to restore the original branch and
working tree. Confirm afterward with `git status` / current branch if unsure.

Finally it prints a summary table of every commit it measured, ordered by
ancestry (oldest first) so the fast → slow transition is visible, with the first
bad commit highlighted (`>>>` gutter, `<- first bad commit` tag, bold-red on a
TTY). Columns show the WebKit commit identifier, the run count, mean time, and (in
t-test mode) the p-value vs baseline; the baseline commit itself shows `base`:

```
Build-time bisect summary (test: clean, runs: 3, alpha: 0.05)

    COMMIT   IDENTIFIER   RUNS    MEAN  P-VALUE  CACHED  VERDICT  SUBJECT
    5667a51  318105@main     3   99.0s     base       —  base     ...
    1c9b298  318106@main     3  100.0s    0.288       —  good     ...
>>> 82c477d  318107@main     3  131.0s  2.5e-06     3/3  bad      ...  <- first bad commit
    90ea4b1  318108@main     3  129.7s  2.4e-04       —  bad      ...

Baseline: 5667a51 (good endpoint), 99.0s mean over 3 runs
First bad commit: 82c477d (318107@main) ...
```

The IDENTIFIER column is the `commits.webkit.org` identifier from the commit's
`Canonical link:` trailer — what bug reports and revert requests quote. A commit
that never landed upstream (local work, a branch built from a patch) has no
identifier, so its committer date is shown instead.

The table includes the good/bad endpoints (t-test mode always; single-run mode
only when auto-calibrating). In single-run mode the P-VALUE column is `—`.
Commits whose build failed to compile render as `skip`.

## Notes

- Runs `measure-build-time` with stdin closed, so the `clean` test deletes the
  build directory without an interactive prompt.
- `Tools/Scripts/bisect-build-time` is a thin entry point; the implementation is
  `Tools/Scripts/buildtime.py` (stdlib only). Python callers can `import
  buildtime`, build the shared command line with `build_parser()`, and call
  `run_driver`/`run_harness` with a `hook(commit_sha, writer) -> bool` callable
  (see "Extending it" above).
- The per-commit harness runs from a copy of the entry script *and* `buildtime.py`
  in a temporary directory, so every commit is measured by the version you
  launched — bisecting across commits that change or predate either file is safe.
- `tqdm` is an optional dependency; without it the `--progress` bar is silently
  skipped (`pip3 install tqdm` to enable). `measure-build-time`'s output is
  captured and streamed through the bar (via `tqdm.write`) in both the
  calibration and bisect phases, so build logs appear beneath the bar without
  corrupting it.
- `--measure-build-time <path>` (or `MEASURE_BUILD_TIME=<path>`) overrides the
  benchmark script — used to test the bisect harness against a stub without doing
  real builds. The driver resolves it once and passes it to the harness, which
  runs from a temp directory and so cannot find it on its own.
- Run `Tools/Scripts/bisect-build-time --help` for the full argument list.
