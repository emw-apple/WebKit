# webkitbuilddiff

`compare-builds` detects performance-impacting differences between two WebKit
builds, so that build-performance comparisons are apples-to-apples (and,
secondarily, so functional divergence is caught early). It has two modes:

- **Xcode vs Ninja** (default) — validate that WebKit's Xcode and CMake/Ninja
  builds are doing equivalent work.
- **Same build system** (`--a`/`--b`) — compare any two build directories,
  detecting each one's build system. Useful for validating a large CMake change
  by diffing a baseline build against one with the change applied.

It compares four dimensions, in decreasing granularity:

| Dimension | Source of truth |
|-----------|-----------------|
| **Targets / binaries** produced | build graph (Xcode `manifest.json`, Ninja `build.ninja`) |
| **Compiler / linker flags** | build graph (Xcode `*-common-args.resp` + `Ld` args, Ninja `compile_commands.json` + link vars) |
| **Exported symbols** | the produced binary (`dyld_info`, via `webkitapipy`) |
| **Codesigning** (identity, flags, entitlements) | the produced binary (`codesign -d`) |

Compiler flags are compared **separately per language** (clang vs Swift), since
`-D`, `-O`, and `-I` mean different things to each front-end. Note that Xcode's
`manifest.json` does not record Swift frontend arguments (they live in the
execution store), so the Swift comparison shows the Ninja build's flags with a
note; clang is compared symmetrically on both sides.

Everything joins on a product's **identity** (its leaf name, e.g.
`JavaScriptCore.framework`), never on target names — the two build systems name
and factor targets differently.

## Usage

```sh
# Auto-discovers <repo>/WebKitBuild/Debug (Xcode) and
# <repo>/WebKitBuild/cmake-mac/Debug (Ninja); compares all four dimensions.
Tools/Scripts/compare-builds

# Compare two builds from the same build system (e.g. validate a CMake change):
Tools/Scripts/compare-builds --a WebKitBuild/cmake-baseline \
                             --b WebKitBuild/cmake-mychange \
                             --label-a baseline --label-b mychange

# One dimension, one product, machine-readable:
Tools/Scripts/compare-builds --only symbols --target JavaScriptCore.framework
Tools/Scripts/compare-builds --json --json-out diff.json

# Self-contained HTML report (inline CSS/JS, filterable, collapsible):
Tools/Scripts/compare-builds --html-out diff.html && open diff.html
```

In generic mode each `--a`/`--b` directory is classified independently: it's a
Ninja build if it contains `build.ninja`, otherwise an Xcode build discovered
via the sibling `XCBuildData` manifest. Unlike the default mode, SDK/arch flags
are compared normally (not treated as equal-by-construction), so a genuine SDK
or deployment-target change surfaces — builds with an identical SDK/toolchain
cancel it out.

Key options: `--a`/`--b` + `--label-a`/`--label-b` (generic mode);
`--xcode-build`, `--ninja-build`, `--manifest` (default mode); `--arch`
(default `arm64e`), `--only targets,args,symbols,codesign`, `--target IDENTITY`
(repeatable), `--json` / `--json-out`, `--html` / `--html-out`, `-v`. In the
text and HTML reports `-` (red) marks something present only in build **A**
(Xcode in the default mode), `+` (green) only in build **B** (Ninja by default).
Exit code is `0` when the builds match, `1` when they differ, `2` on a usage/IO
error.

## Development

```sh
cd Tools/Scripts/libraries/webkitbuilddiff
python3 -m unittest discover -s webkitbuilddiff -p '*_unittest.py'
python3 -m mypy webkitbuilddiff
```
