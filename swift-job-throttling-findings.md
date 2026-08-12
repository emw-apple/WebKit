# Swift job-count throttling in the CMake build

Findings from investigating an apparent C++ compile-time regression on
`eng/cmake-prewarm-fix`, which turned into a scheduling fix worth 15% of a clean
build. Scratch notes; move somewhere real if this lands.

## TL;DR

- The prewarm commit did **not** make C++ compilation slower. Per-TU cost is
  unchanged; the ninja-log numbers that suggested otherwise were contention
  inflation.
- CMake hands `swiftc` `-j <ncpu> -num-threads <ncpu>` while ninja books the whole
  Swift compile as **one** job, so a single Swift edge draws ~10 cores against a
  full complement of clang edges.
- Capping it at `-j 12` on a 16-core machine is worth **−59.3s (−15.5%)** on a
  clean build, with no cost when the machine is otherwise idle.
- `12` is optimal because the module has 23 Swift files and wall time is quantized
  into `ceil(23 / j)` waves. 12 is the smallest `-j` that still fits 2 waves.

## Context

`ninja-log-diff` on before/after logs of the prewarm commit showed ~222 edges
slower, including every WebKit unified source at roughly 2x, e.g.
`UnifiedSource-UIProcess-5.cpp.o` at 22.4s → 46.7s. Surprising, since the commit
only touches Swift module prewarming.

It was not a real regression:

- The only C++-visible change in the commit is libwebrtc include dirs moving from
  `WebKit_SYSTEM_INCLUDE_DIRECTORIES` to `WebKit_PRIVATE_INCLUDE_DIRECTORIES`
  (`-isystem` → `-I`). A/B on the real compile command for one of the worst
  "regressed" TUs: **11.67s / 10.93s / 11.01s / 11.08s**, zero warnings either
  way. Inert.
- That same TU takes **11s in isolation**, 22.4s in the "good" build, and 46.7s in
  the "bad" one. Both builds are contention-inflated; the delta is just more of it.
- Total wall only moved 5m26s → 5m40s (+14.7s), while *summed* edge duration moved
  +14m57s. On 16 cores, 15 extra core-minutes of real work cannot cost 14.7s of
  wall.

The mechanism: `ninja -j` defaults to `ncpu + 2` and counts a Swift edge as one
job, but `CMakeSwiftInformation.cmake` bakes `-j <ncpu> -num-threads <ncpu>` into
the Swift rule (overridable via `CMAKE_Swift_NUM_THREADS`). Measured over the
window where WebKit's C++ and Swift overlap:

| build | concurrent heavy clang edges | cores drawn by the Swift edge | total |
|---|---|---|---|
| before | 10.5 | 5.6 | ~16 on 16 cores |
| after | 15.3 | 9.2 | ~24 on 16 cores |

The prewarm fix is a genuine ~124 CPU-second win (the Swift edge's CPU drops from
530.9s to 406.6s once modules are actually prewarmed), but it also removed a
serial PCM-building prefix that had been keeping the wide Swift fan-out out of the
way of the C++ wave. The work got cheaper and the scheduling got worse.

## Method

16-core M4 Max (12P + 4E). Clean builds via `ninja clean` (which also wipes
`SwiftModuleCache`, so prewarm does real work each time), 3 runs per config, config
order rotated per round so drift spreads evenly. Load average recorded before each
build. Three of the twelve runs were hit by external load (2525s and 3568s
outliers, contiguous in time, with the same configs fine in adjacent rounds); those
are quarantined and were re-run, not dropped.

Uncontended numbers are the Swift edge alone, with a full recompile forced by
deleting `WebKit.dir/Debug/WebKit.priors`, driven through `WEBKIT_SWIFT_JOBS`.

## Job sizes and their impact

### Contended (full clean build, n=3)

| `-j` | build wall | Δ vs default | Swift edge | WebKit C++ Σ | prewarm |
|---|---|---|---|---|---|
| 16 *(CMake default)* | 381.8s | — | 145.8s | 944.7s | 71.5s |
| **12** | **322.5s** | **−59.3s (−15.5%)** | **89.5s** | 935.2s | 70.9s |
| 10 | 341.7s | −40.1s (−10.5%) | 104.2s | 980.4s | 71.6s |
| 8 | 348.3s | −33.5s (−8.8%) | 106.9s | 982.3s | 73.3s |

Per-run wall, to show the spread:

```
16   378.6  376.7  390.1     (sigma 7.4)
12   322.1  320.8  324.6     (sigma 1.9)
10   335.0  346.2  343.8
 8   340.4  343.5  361.0
```

### Uncontended (Swift edge alone, n=2)

| `-j` | edge wall | cores drawn |
|---|---|---|
| 16 | 43.2s | 9.9 |
| **12** | **41.0s** | 9.6 |
| 10 | 54.4s | 7.0 |
| 8 | 57.5s | 6.5 |
| 6 | 71.1s | 5.2 |

**12 is optimal in both regimes** — the clean-build win comes with no idle-machine
cost, and in fact saves ~36s of CPU there too.

## Why 12

Wall time is quantized into `ceil(nfiles / j)` waves of roughly 20s each. With 23
Swift files:

```
 j  waves  measured   per-wave
16      2    43.2s      21.6s
12      2    41.0s      20.5s
10      3    54.4s      18.1s
 8      3    57.5s      19.2s
 6      4    71.1s      17.8s
```

12 is the smallest `-j` that still fits **2 waves**. Above it you buy no throughput
and only add contention pressure; below it you pay an entire extra wave. That
accounts for the whole uncontended curve.

Contention adds a second, independent effect. `-j 16` and `-j 12` are both 2 waves,
but against ~15 concurrent clang edges the 16-frontend version inflates 3.4x over
its isolated time (145.8s vs 43.2s) while the 12-frontend version inflates only
2.2x (89.5s vs 41.0s). So 12 wins twice: same wave count, materially less
thrashing among the swift-frontend processes.

Two further observations:

- **The whole effect lands on wall time.** The Swift edge starts at ~213s
  regardless of config, and the tail after it is a fixed 19–21s in every run, so
  its duration maps ~1:1 onto total build time.
- **It is not a trade against C++.** Summed WebKit-family C++ time is flat across
  configs (935–982s), and prewarm/PAL/WebGPU stay inside their normal spread. This
  is removing self-thrashing among Swift frontends, not robbing clang.

## The change

Two wrappers, layered so each does one thing.

**`Source/cmake/ninja-wrapper`** already dry-runs the build to pick a unified-sources
bundle policy, so `$totalCommands` is free. It now also classifies build size for
Swift and exports `WK_SWIFT_JOBS_POLICY` (`Throttled` / `Full`), recording it to
`DerivedSources/swift-jobs-policy` for inspection. The two policies are computed
under independent `defined()` guards so presetting one doesn't suppress the other.

**`Tools/Scripts/swift/swiftc-wrapper.sh`** rewrites `-j` / `-num-threads` in a
post-pass over the assembled argv — a post-pass rather than inside the existing
translation loop because `-j` arrives at the front of the command line from the
rule while `-module-name` arrives later via the target's compile flags.
Resolution order:

```
WEBKIT_SWIFT_JOBS_<Module>  ->  WEBKIT_SWIFT_JOBS  ->  policy (0.75 x ncpu unless "Full")  ->  CMake's value
```

Throttling is the **default**, so a plain `ninja` (which bypasses `ninja-wrapper`)
still gets the clean-build win. `ninja-wrapper` opts small builds back out with
`Full`; a malformed value falls through to CMake's default rather than wedging the
build.

Verified: all six precedence cases; classification at the 500/501 boundary against
a stub ninja; the no-op early-exit and existing bundle-policy behavior unchanged;
`perl -c` and `bash -n` clean; the Swift edge reproducing at 90.1s through the real
wrapper end-to-end; and **no `-emit-pcm` jobs** with `-j` rewritten, i.e. the
throttle is inert to the module cache key, so prewarming still hits.

End-to-end, through `ninja-wrapper`:

| | policy | wall |
|---|---|---|
| clean build | `Throttled` | 313.1s |
| Swift-only incremental (1 edge) | `Full` | 19s |

A one-file Swift edit via plain `ninja` is 17s throttled vs 18s full — the cap
doesn't bind, because swift-driver only recompiles what changed.

## Not validated

- **`0.75 x ncpu = 12` is right here by coincidence.** The real driver is file
  count, not core count. On an 8-core machine the formula yields 6 → 4 waves →
  ~30s slower uncontended. The floor matters more than the fraction. The wrapper
  has the Swift file list in `args`, so a file-count-aware floor such as
  `max(fraction x ncpu, ceil(nfiles / 2))` is implementable — but there's no data
  from a small-core machine yet, so it shouldn't be changed blind.
- **Release / `-wmo` is unmeasured.** There `-num-threads` carries the parallelism
  rather than `-j`, and the wave model may not apply. The wrapper rewrites both.
- **Explicit `ninja -j N` isn't factored in.** Classification uses work *volume*,
  not the concurrency actually granted. At `-j 4` you'd get 4 clang + 12 Swift,
  which is coincidentally fine; at `-j 32` it would be worse. Reading `-j` out of
  `@ARGV` would close that.
- Single machine, single OS version, Debug only.

## Measurement traps

Each of these produced a confidently wrong answer during this investigation.

1. **Summed edge duration is not CPU time.** It is a sum of wall-clock durations
   and rises with contention. The original "regression" showed +15 core-minutes
   for +14.7s of wall on 16 cores. Compare wall time only.
2. **Measuring an edge in isolation can invert the sign of a scheduling change.**
   Timing the Swift edge alone said throttling would cost wall time, which was
   used to argue a static default was unshippable. Contention was the entire
   subject; removing it removed the effect.
3. **A build-system change can silently override the experiment's knob.** Once the
   wrapper defaulted to throttled, a harness that rewrote `-j` in argv was
   overridden by the wrapper, so a batch of runs were all secretly `-j 12` and
   looked identical. Tell-tale: measured parallelism didn't track the flag —
   "`-j 6`" reported 9.7 cores, which is impossible. Drive the real knob.

## Data

`~/swift-jobs-sweep/` — per-run ninja logs (`ninja_log-<config>-r<round>`),
`results.txt` with wall time and load-before per run, `analyze.py`, `sweep.sh`
(resumable: it skips any config/round whose log already exists), and
`contaminated/` for the externally-perturbed runs.
