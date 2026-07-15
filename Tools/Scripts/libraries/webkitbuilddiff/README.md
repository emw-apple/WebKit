# webkitbuilddiff

`compare-builds` detects performance-impacting differences between WebKit's
**Xcode** and **CMake/Ninja** builds, so that build-performance comparisons
between the two systems are apples-to-apples (and, secondarily, so functional
divergence is caught early).

It compares four dimensions, in decreasing granularity:

| Dimension | Source of truth |
|-----------|-----------------|
| **Targets / binaries** produced | build graph (Xcode `manifest.json`, Ninja `build.ninja`) |
| **Compiler / linker flags** | build graph (Xcode `*-common-args.resp` + `Ld` args, Ninja `compile_commands.json` + link vars) |
| **Exported symbols** | the produced binary (`dyld_info`, via `webkitapipy`) |
| **Codesigning** (identity, flags, entitlements) | the produced binary (`codesign -d`) |

Everything joins on a product's **identity** (its leaf name, e.g.
`JavaScriptCore.framework`), never on target names — the two build systems name
and factor targets differently.

## Usage

```sh
# Auto-discovers <repo>/WebKitBuild/Debug (Xcode) and
# <repo>/WebKitBuild/cmake-mac/Debug (Ninja); compares all four dimensions.
Tools/Scripts/compare-builds

# One dimension, one product, machine-readable:
Tools/Scripts/compare-builds --only symbols --target JavaScriptCore.framework
Tools/Scripts/compare-builds --json --json-out diff.json

# Self-contained HTML report (inline CSS/JS, filterable, collapsible):
Tools/Scripts/compare-builds --html-out diff.html && open diff.html
```

Key options: `--xcode-build`, `--ninja-build`, `--manifest`, `--arch`
(default `arm64e`), `--only targets,args,symbols,codesign`, `--target IDENTITY`
(repeatable), `--json` / `--json-out`, `--html` / `--html-out`, `-v`. In the
text and HTML reports `-` (red) marks something present only in Xcode, `+`
(green) only in Ninja. Exit code is `0` when the builds match, `1` when they
differ, `2` on a usage/IO error.

## Development

```sh
cd Tools/Scripts/libraries/webkitbuilddiff
python3 -m unittest discover -s webkitbuilddiff -p '*_unittest.py'
python3 -m mypy webkitbuilddiff
```
