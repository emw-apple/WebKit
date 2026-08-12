# Swift `-j` tuning harness

Measures how `swiftc`'s frontend job count affects a WebKit CMake build, so the
default in `Tools/Scripts/swift/swiftc-wrapper.sh` can be calibrated per machine.
See `../swift-job-throttling-findings.md` for the results this produced on a
16-core M4 Max.

The thing being tuned: CMake hands `swiftc` `-j <ncpu> -num-threads <ncpu>` while
ninja books the whole Swift compile as **one** job, so a single Swift edge can draw
~10 cores against a full complement of clang edges. Capping it helps a clean build
but can cost an idle-machine rebuild, so both regimes have to be measured.

## Usage

```sh
cd /path/to/WebKit
python3 swift-jobs-tuning/tune.py plan          # cheap; validates discovery
python3 swift-jobs-tuning/tune.py edge          # ~10 min: uncontended regime
python3 swift-jobs-tuning/tune.py sweep         # ~1 hr: contended regime (clean builds)
python3 swift-jobs-tuning/tune.py report
```

Options: `--build <dir>` (default `WebKitBuild`, or `$WEBKIT_BUILD_DIR`),
`--module <name>` (default `WebKit`), `--out <dir>` (default
`~/swift-jobs-tuning-data`), `--rounds N` (sweep, default 3), `--runs N` (edge,
default 2), `--dry-run`.

`sweep` is resumable — it skips any config/round whose ninja log already exists, so
you can stop it and pick up later, or delete one log to re-run just that cell.

`plan` picks the `-j` values to test from the core count and the module's Swift file
count: CMake's default (`ncpu`, the control), the **wave-optimal** value, and
`0.75 x ncpu` / `0.5 x ncpu`. Duplicates collapse, so you usually get 3–4 configs.

## What "wave-optimal" means

Swift edge wall time is quantized: with `n` files and `-j`, swift-driver needs
`ceil(n / j)` waves of frontend processes, each costing roughly the same. So the
best `-j` is the **smallest** value that still achieves the minimum wave count —
lower adds a whole wave, higher buys nothing and only adds contention pressure.

On the 16-core machine, 23 files gave `ceil(23/12) = 2` waves, making 12 optimal;
`0.75 x ncpu` happened to equal 12 as well. **Those coincide by accident.** On an
8-core machine `0.75 x ncpu` is 6, which needs 4 waves and is ~30s slower. That
divergence is the main reason to re-run this per machine.

`report` prints a per-wave column: if it is roughly constant across configs, the
quantization model holds on your machine too.

## Please leave the machine idle

The whole measurement is about CPU contention, so background load corrupts it.
`sweep` records the load average before each build and `report` flags runs more
than 1.5x the median as likely-external-load outliers and excludes them from the
means. On the original run, 3 of 12 builds were hit this way (2525s and 3568s
against a 322s norm) — re-run those cells rather than trusting them.

## Traps this harness is built to avoid

Each of these produced a confidently wrong answer during the original
investigation:

1. **Summed edge duration is not CPU time.** Ninja logs record wall-clock per
   edge, which inflates under contention. A change that cost +14.7s of wall showed
   +15 *core-minutes* of summed edge duration on 16 cores. `report` only compares
   total wall time.
2. **Timing the edge in isolation can invert the sign of a scheduling change.**
   That is why `edge` and `sweep` are separate regimes and both are reported;
   neither alone tells you what to ship.
3. **An unset `WEBKIT_SWIFT_JOBS` does not mean "CMake's default."**
   `swiftc-wrapper.sh` throttles by default, so the control config must set the
   value explicitly. `sweep` and `edge` always set it, and clear
   `WK_SWIFT_JOBS_POLICY` so `ninja-wrapper`'s classification cannot interfere.
   Getting this wrong silently makes every config identical; the tell-tale is
   measured parallelism not tracking the flag.
4. **`swift-driver` no-ops if its build record survives.** `edge` deletes the
   module's `.priors` file and one declared output of the edge, so ninja re-runs it
   and swift-driver does a full recompile rather than finishing in 7 seconds.
5. **Replaying a captured compile command bypasses the dependency graph.** `edge`
   drives `ninja <target>` instead, and brings the target up to date first, so a
   tree that has drifted (missing forwarding headers, stale derived sources) fails
   loudly up front instead of mid-measurement.

## Reporting back

`--out` contains everything needed to compare machines: `sweep.jsonl` / `edge.jsonl`
(one record per run, each stamped with model, core counts and OS) and the per-run
ninja logs. `report` is safe to run against partial data.
