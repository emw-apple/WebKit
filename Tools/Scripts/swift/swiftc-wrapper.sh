#!/bin/bash
# cmake accumulates CFLAGS from pkg-config, and then passes them to swiftc.
# This script filters out the arguments that swiftc cannot accommodate.

set -e
set -o pipefail

# Swift's C++ interop changes which imported members are @unsafe between
# toolchain versions, so an `unsafe` that is required on one toolchain emits
# "no unsafe operations occur within 'unsafe' expression" on another. The
# diagnostic has no group, so it can't be suppressed with -Wwarning; filter it
# (and its multi-line source snippet) from stderr instead.
filter_benign_warnings() {
    awk '
        /: warning: no unsafe operations occur within .unsafe. expression/ { skip = 1; next }
        skip && /^[[:space:]]*[0-9]*[[:space:]]*\|/ { next }
        skip && /^[[:space:]]*$/ { skip = 0; next }
        { skip = 0; print }
    '
}

REAL_SWIFTC=swiftc
args=()

# CMake's Swift link rule injects <LANGUAGE_COMPILE_FLAGS> into the link
# command, which includes -g, which causes swiftc to run dsymutil.
# dsymutil is super expensive, and we don't need it because we have DWARF
# debug info in our object files.
linking=
for arg in "$@"; do
    case "$arg" in
        "-emit-library"|"-emit-executable") linking=1 ;;
    esac
done

for arg in "$@"; do
    if [[ "$arg" == @*.platform-swift-args.resp && -f "${arg#@}" ]]; then
        # Expand our resp in-process: the swift driver doesn't expand @-files
        # under -explicit-module-build, and emitting tokens directly bypasses
        # the case-statement's -D doubling — which would otherwise leak
        # Platform.h-derived defines into the clang importer. Other @-files
        # (CMake's link/compile rsp) pass through; swiftc expands them.
        while IFS= read -r line; do
            [[ -z "$line" ]] && continue
            args+=("$line")
        done < "${arg#@}"
        continue
    fi
    if [[ -n "$pass_next_verbatim" ]]; then
        args+=("$arg")
        pass_next_verbatim=
        continue
    fi
    case "$arg" in
        "-Xcc"|"-Xlinker"|"-Xfrontend")
            args+=("$arg")
            pass_next_verbatim=1
            ;;
        "-mfpmath=sse") ;;
        "-msse") ;;
        "-msse2") ;;
        "-pthread") ;;
        "-fsanitize="*)
            args+=("-sanitize=${arg#-fsanitize=}")
            ;;
        "-g")
            if [[ -z "$linking" ]]; then
                args+=("$arg")
            fi
            ;;
        "-include") skip_next=1 ;;
        "-flto" | "-flto="*)
            args+=("-Xcc" "$arg")
            ;;
        "-fuse-ld="*)
            args+=("-Xcc" "$arg")
            ;;
        # swiftc does not understand clang-specific include flags like
        # -isystem / -iquote / -idirafter; wrap them (and their following
        # path argument) as -Xcc so they reach the Clang importer instead
        # of being rejected at parse time.
        "-isystem"|"-iquote"|"-idirafter"|"-isysroot")
            args+=("-Xcc" "$arg" "-Xcc")
            pass_next_verbatim=1
            ;;
        # CMake leaks clang linker flags into swiftc; translate them.
        "-compatibility_version"|"-current_version")
            args+=("-Xlinker" "$arg")
            skip_next_as_xlinker=1
            ;;
        "-weak_framework")
            args+=("-Xlinker" "-weak_framework")
            skip_next_as_xlinker=1
            ;;
        "-Wl,"*)
            # Split -Wl,arg1,arg2 into -Xlinker arg1 -Xlinker arg2
            IFS=',' read -ra _wl_args <<< "${arg#-Wl,}"
            for _wl in "${_wl_args[@]}"; do
                args+=("-Xlinker" "$_wl")
            done
            ;;
        "--original-swift-compiler="*)
            REAL_SWIFTC="${arg#--original-swift-compiler=}"
            ;;
        "-D"*"="*)
            # Swift conditional-compilation flags are valueless; the importer's
            # -D set comes from _WEBKIT_COMPUTE_SWIFT_SHARED_CLANG_FLAGS, so
            # valued target_compile_definitions are dropped here.
            ;;
        *)
            if [[ -n "$skip_next" ]]; then
                skip_next=
            elif [[ -n "$skip_next_as_xlinker" ]]; then
                args+=("-Xlinker" "$arg")
                skip_next_as_xlinker=
            else
                args+=("$arg")
            fi
            ;;
    esac
done

# CMake's Swift rules hardcode `-j <ncpu> -num-threads <ncpu>`, but ninja books
# the whole Swift compile as a single job, so one Swift edge can draw every core
# while ninja concurrently schedules a full complement of clang edges. Throttle by
# default so that a plain `ninja` still benefits; ninja-wrapper opts small builds
# back out, and WEBKIT_SWIFT_JOBS[_<module>] overrides either way.
#
# This runs as a post-pass rather than inside the loop above because `-j` comes
# from the rule at the front of the command line, while -module-name arrives
# later by way of the target's compile flags.
module_name=
for i in "${!args[@]}"; do
    if [[ "${args[i]}" == "-module-name" ]]; then
        module_name="${args[i + 1]}"
        break
    fi
done

swift_jobs=
if [[ -n "$module_name" ]]; then
    jobs_var="WEBKIT_SWIFT_JOBS_${module_name//[^A-Za-z0-9_]/_}"
    swift_jobs="${!jobs_var}"
fi
: "${swift_jobs:=$WEBKIT_SWIFT_JOBS}"

# Three quarters of the cores leaves room for the concurrent clang edges, so
# neither side thrashes.
if [[ -z "$swift_jobs" && "$WK_SWIFT_JOBS_POLICY" != "Full" ]]; then
    ncpu=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo)
    if [[ "$ncpu" =~ ^[1-9][0-9]*$ ]]; then
        swift_jobs=$((ncpu * 3 / 4))
        if [ "$swift_jobs" -lt 2 ]; then
            swift_jobs=2
        fi
    fi
fi

# A malformed value falls through to CMake's default instead of wedging the build.
if [[ "$swift_jobs" =~ ^[1-9][0-9]*$ ]]; then
    for i in "${!args[@]}"; do
        case "${args[i]}" in
            "-j"|"-num-threads")
                # Only rewrite a numeric operand, so that a -Xcc-wrapped token
                # which happens to spell one of these can't be clobbered.
                if [[ "${args[i + 1]}" =~ ^[0-9]+$ ]]; then
                    args[i + 1]="$swift_jobs"
                fi
                ;;
        esac
    done
fi

{ "$REAL_SWIFTC" "${args[@]}" 2>&1 1>&3 | filter_benign_warnings >&2; } 3>&1
