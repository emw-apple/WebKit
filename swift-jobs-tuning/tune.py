#!/usr/bin/env python3
"""Tune swiftc's -j for a WebKit CMake build.

Subcommands:
  plan     Discover the build, print machine info and the -j values to test.
  sweep    Clean-build sweep across -j values (the contended regime).
  edge     Time the Swift edge alone across -j values (the uncontended regime).
  report   Tables for whatever sweep/edge data exists.

See README.md. Run `plan` first; it is cheap and validates discovery.
"""

import argparse
import glob
import json
import math
import os
import resource
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path


def sh(cmd, cwd=None):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)


def sysctl(name):
    r = sh(["sysctl", "-n", name])
    return r.stdout.strip() if r.returncode == 0 else None


def machine_info():
    info = {"ncpu": None, "model": sysctl("hw.model"), "os": None}
    ncpu = sysctl("hw.ncpu")
    if not (ncpu or "").isdigit():
        r = sh(["nproc"])
        ncpu = r.stdout.strip() if r.returncode == 0 else None
    info["ncpu"] = int(ncpu) if (ncpu or "").isdigit() else (os.cpu_count() or 1)
    info["perf_cores"] = sysctl("hw.perflevel0.logicalcpu")
    info["eff_cores"] = sysctl("hw.perflevel1.logicalcpu")
    r = sh(["sw_vers", "-productVersion"])
    if r.returncode == 0:
        info["os"] = r.stdout.strip()
    return info


def load_average():
    return (sysctl("vm.loadavg") or "").strip("{} ")


# --- discovery ---------------------------------------------------------------

def discover(build, module):
    """Find the Swift edge for `module`, its compile command, and its file count."""
    r = sh(["ninja", "-t", "targets", "all"], cwd=build)
    if r.returncode != 0:
        sys.exit(f"ninja -t targets failed in {build}:\n{r.stderr}")
    targets = [ln.split(":", 1)[0] for ln in r.stdout.splitlines()
               if ln.split(":", 1)[0].endswith(f"{module}.swiftmodule")]
    if not targets:
        sys.exit(f"no <...>{module}.swiftmodule target in {build}; pass --module")
    target = min(targets, key=len)

    r = sh(["ninja", "-t", "commands", target], cwd=build)
    if r.returncode != 0 or not r.stdout.strip():
        sys.exit(f"could not get the compile command for {target}")
    command = r.stdout.strip().splitlines()[-1]
    argv = shlex.split(command)
    nfiles = sum(1 for a in argv if a.endswith(".swift"))
    if not nfiles:
        sys.exit(f"no .swift sources in the command for {target}")

    # Deleting the incremental build record is what forces a full recompile;
    # without it swift-driver no-ops in a few seconds and every number is a lie.
    priors_glob = str(Path(build) / "**" / f"{module}.priors")

    return {"target": target, "command": command, "argv": argv,
            "nfiles": nfiles, "priors_glob": priors_glob,
            # Source/WebKit/WebKit.swiftmodule -> Source/WebKit/
            "source_prefix": str(Path(target).parent) + "/"}


def job_counts(ncpu, nfiles):
    """The -j values worth testing.

    Wall time is quantized into ceil(nfiles / j) waves, so the interesting value
    is the smallest j that still achieves the minimum wave count. Fractions of
    ncpu are included because that is what a shipped default would likely use,
    and on this machine the two happened to coincide at 12.
    """
    min_waves = max(1, math.ceil(nfiles / ncpu))
    wave_optimal = math.ceil(nfiles / min_waves)
    candidates = {ncpu, wave_optimal, ncpu * 3 // 4, ncpu // 2}
    return sorted(c for c in candidates if 2 <= c <= ncpu)


def waves(nfiles, j):
    return math.ceil(nfiles / j)


# --- ninja log parsing -------------------------------------------------------

def load_ninja_log(path):
    edges = {}
    with open(path) as fh:
        for line in fh:
            if line.startswith("#"):
                continue
            f = line.rstrip("\n").split("\t")
            if len(f) < 5:
                continue
            edges.setdefault((int(f[0]), int(f[1]), f[4]), []).append(f[3])
    return {min(outs, key=len): (s, e) for (s, e, _), outs in edges.items()}


def log_metrics(path, info):
    d = load_ninja_log(path)
    if not d:
        return None
    dur = lambda k: (d[k][1] - d[k][0]) / 1000
    m = {"wall": max(e for _, e in d.values()) / 1000, "swift": None,
         "cxx_sum": 0.0, "prewarm": None}
    for k in d:
        if k.endswith(info["target"]):
            m["swift"] = dur(k)
        elif k.endswith(".swiftmodule") and "Prewarm" in k:
            m["prewarm"] = dur(k)
        elif k.endswith(".o") and info["source_prefix"] in k:
            m["cxx_sum"] += dur(k)
    if m["swift"] is not None:
        s, e = next(v for k, v in d.items() if k.endswith(info["target"]))
        m["tail_after_swift"] = m["wall"] - e / 1000
    return m


# --- subcommands -------------------------------------------------------------

def append_result(out, name, record):
    with open(Path(out) / name, "a") as fh:
        fh.write(json.dumps(record) + "\n")


def read_results(out, name):
    path = Path(out) / name
    if not path.exists():
        return []
    return [json.loads(ln) for ln in path.read_text().splitlines() if ln.strip()]


def cmd_plan(args, info, mach):
    js = job_counts(mach["ncpu"], info["nfiles"])
    print(f"machine      {mach['model']}, {mach['ncpu']} cores "
          f"({mach['perf_cores']}P + {mach['eff_cores']}E), macOS {mach['os']}")
    print(f"build dir    {args.build}")
    print(f"swift edge   {info['target']}")
    print(f"swift files  {info['nfiles']}")
    found = glob.glob(info["priors_glob"], recursive=True)
    print(f"build record {found[0] if found else '(none yet; build once first)'}")
    print(f"output dir   {args.out}")
    print()
    print(f"{'-j':>4}  {'waves':>5}  note")
    for j in js:
        note = []
        if j == mach["ncpu"]:
            note.append("control (CMake's default)")
        if j == math.ceil(info["nfiles"] / max(1, math.ceil(info["nfiles"] / mach["ncpu"]))):
            note.append("wave-optimal")
        if j == mach["ncpu"] * 3 // 4:
            note.append("0.75 x ncpu")
        if j == mach["ncpu"] // 2:
            note.append("0.50 x ncpu")
        print(f"{j:>4}  {waves(info['nfiles'], j):>5}  {', '.join(note)}")
    print()
    print(f"sweep: {len(js)} configs x {args.rounds} rounds = "
          f"{len(js) * args.rounds} clean builds")


def cmd_sweep(args, info, mach):
    js = job_counts(mach["ncpu"], info["nfiles"])
    for r in range(1, args.rounds + 1):
        # Rotate so thermal/background drift spreads across configs instead of
        # landing on whichever one always runs first.
        order = js[(r - 1) % len(js):] + js[:(r - 1) % len(js)]
        for j in order:
            tag = f"{j}-r{r}"
            log = Path(args.out) / f"ninja_log-{tag}"
            if log.exists():
                print(f"[skip] {tag} (already have {log.name})")
                continue
            if args.dry_run:
                print(f"[dry-run] ninja clean; WEBKIT_SWIFT_JOBS={j} ninja  -> {log.name}")
                continue

            print(f"[run] {tag}: cleaning...", flush=True)
            sh(["ninja", "clean"], cwd=args.build)
            # Always set the value explicitly, including for the control. An
            # unset variable does NOT mean "CMake's default" -- swiftc-wrapper
            # throttles by default -- and conflating the two silently voids the
            # whole experiment.
            env = dict(os.environ, WEBKIT_SWIFT_JOBS=str(j))
            env.pop("WK_SWIFT_JOBS_POLICY", None)
            before = load_average()
            start = time.time()
            p = subprocess.run(["ninja"], cwd=args.build, env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            wall = time.time() - start
            shutil.copy(Path(args.build) / ".ninja_log", log)
            append_result(args.out, "sweep.jsonl",
                          {"j": j, "round": r, "rc": p.returncode,
                           "wall": round(wall, 1), "load_before": before,
                           "machine": mach})
            print(f"[run] {tag}: rc={p.returncode} wall={wall:.0f}s")
    print("sweep complete")


def cmd_edge(args, info, mach):
    js = job_counts(mach["ncpu"], info["nfiles"])
    target = info["target"]

    if args.dry_run:
        print(f"[dry-run] {len(js)} configs x {args.runs} runs, plus one warm-up")
        return

    # Drive the edge through ninja rather than replaying the captured swiftc
    # command. The command on its own knows nothing about forwarding headers or
    # derived sources, so a tree that has drifted since the last build fails in
    # confusing ways that look like measurement noise. Bring it up to date first,
    # untimed.
    print(f"bringing {target} up to date (untimed)...", flush=True)
    p = sh(["ninja", target], cwd=args.build)
    if p.returncode != 0:
        sys.exit(f"could not build {target}; fix the build first:\n{p.stderr[-2000:]}")

    def run_once(j, label):
        # Removing one declared output is enough to make ninja re-run the edge;
        # removing the build record stops swift-driver no-opping inside it.
        out = Path(args.build) / target
        if out.exists():
            out.unlink()
        for path in glob.glob(info["priors_glob"], recursive=True):
            os.remove(path)

        env = dict(os.environ, WEBKIT_SWIFT_JOBS=str(j))
        env.pop("WK_SWIFT_JOBS_POLICY", None)
        r0 = resource.getrusage(resource.RUSAGE_CHILDREN)
        start = time.time()
        p = subprocess.run(["ninja", target], cwd=args.build, env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        wall = time.time() - start
        r1 = resource.getrusage(resource.RUSAGE_CHILDREN)
        cpu = (r1.ru_utime - r0.ru_utime) + (r1.ru_stime - r0.ru_stime)
        print(f"  {label} -j {j:>3}: wall {wall:6.1f}s  CPU {cpu:6.1f}s  "
              f"parallelism {cpu / wall:4.1f} cores  rc={p.returncode}")
        if p.returncode:
            # Collecting more numbers after a failure just buries the error.
            sys.exit(f"build failed at -j {j}:\n{p.stderr[-2000:]}")
        return {"j": j, "wall": round(wall, 1), "cpu": round(cpu, 1),
                "cores": round(cpu / wall, 1), "rc": p.returncode, "machine": mach}

    # The first full recompile after a lull is much slower than steady state,
    # regardless of -j. Burn one so it does not land on whichever config is first.
    print("warm-up (discarded):")
    run_once(js[-1], "warmup")
    for i in range(1, args.runs + 1):
        print(f"round {i}:")
        for j in js:
            append_result(args.out, "edge.jsonl", run_once(j, f"r{i}   "))
    print("edge measurements complete")


def cmd_report(args, info, mach):
    nfiles = info["nfiles"]

    sweep = [r for r in read_results(args.out, "sweep.jsonl") if r.get("rc") == 0]
    if sweep:
        by_j = {}
        for r in sweep:
            log = Path(args.out) / f"ninja_log-{r['j']}-r{r['round']}"
            m = log_metrics(log, info) if log.exists() else None
            by_j.setdefault(r["j"], []).append({**r, **(m or {})})
        walls = [x["wall"] for xs in by_j.values() for x in xs]
        median = sorted(walls)[len(walls) // 2]
        print("=== contended (clean build) ===")
        print(f"{'-j':>4} {'waves':>5} {'n':>2} {'wall':>8} {'delta':>9} "
              f"{'swift':>8} {'C++ sum':>9} {'tail':>6}")
        control = None
        for j in sorted(by_j):
            xs = [x for x in by_j[j] if x["wall"] <= median * 1.5]
            dropped = len(by_j[j]) - len(xs)
            if not xs:
                continue
            mean = lambda k: sum(x[k] for x in xs if x.get(k)) / max(1, len([x for x in xs if x.get(k)]))
            w = mean("wall")
            if j == mach["ncpu"]:
                control = w
            delta = f"{w - control:+.1f}s" if control and j != mach["ncpu"] else ""
            print(f"{j:>4} {waves(nfiles, j):>5} {len(xs):>2} {w:7.1f}s {delta:>9} "
                  f"{mean('swift'):7.1f}s {mean('cxx_sum'):8.1f}s {mean('tail_after_swift'):5.1f}s"
                  + (f"   ({dropped} run(s) dropped as outliers)" if dropped else ""))
        print()
        print("per-run wall times (load average before each build):")
        for j in sorted(by_j):
            for x in by_j[j]:
                flag = "  <- outlier, likely external load" if x["wall"] > median * 1.5 else ""
                print(f"  -j {j:>3} r{x['round']}  {x['wall']:7.1f}s   {x.get('load_before','')}{flag}")
        print()

    edge = [r for r in read_results(args.out, "edge.jsonl") if r.get("rc") == 0]
    if edge:
        by_j = {}
        for r in edge:
            by_j.setdefault(r["j"], []).append(r)
        print("=== uncontended (Swift edge alone) ===")
        print(f"{'-j':>4} {'waves':>5} {'n':>2} {'wall':>8} {'per-wave':>9} "
              f"{'CPU':>8} {'cores':>6}")
        for j in sorted(by_j):
            xs = by_j[j]
            w = sum(x["wall"] for x in xs) / len(xs)
            print(f"{j:>4} {waves(nfiles, j):>5} {len(xs):>2} {w:7.1f}s "
                  f"{w / waves(nfiles, j):8.1f}s "
                  f"{sum(x['cpu'] for x in xs) / len(xs):7.1f}s "
                  f"{sum(x['cores'] for x in xs) / len(xs):5.1f}")
        print()
        print("If per-wave is roughly constant, wall time is wave-quantised and the")
        print(f"best -j is the smallest one achieving {waves(nfiles, max(by_j))} wave(s).")

    if not sweep and not edge:
        print(f"no results in {args.out}; run `sweep` and/or `edge` first")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["plan", "sweep", "edge", "report"])
    ap.add_argument("--build", default=os.environ.get("WEBKIT_BUILD_DIR", "WebKitBuild"),
                    help="CMake build directory (default: WebKitBuild)")
    ap.add_argument("--module", default="WebKit", help="Swift module to tune (default: WebKit)")
    ap.add_argument("--out", default=os.path.expanduser("~/swift-jobs-tuning-data"),
                    help="where to write logs and results")
    ap.add_argument("--rounds", type=int, default=3, help="sweep rounds per config")
    ap.add_argument("--runs", type=int, default=2, help="edge runs per config")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    args.build = str(Path(args.build).resolve())
    if not (Path(args.build) / "build.ninja").exists():
        sys.exit(f"{args.build} is not a configured ninja build dir (--build)")
    os.makedirs(args.out, exist_ok=True)

    mach = machine_info()
    info = discover(args.build, args.module)
    {"plan": cmd_plan, "sweep": cmd_sweep, "edge": cmd_edge,
     "report": cmd_report}[args.command](args, info, mach)


if __name__ == "__main__":
    main()
