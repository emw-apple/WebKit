#!/bin/bash
# bash (not sh) is required to support hyphens in function names (the audit-spi
# fallback case below) and for array variables.
set -e

PATH="${SRCROOT}/../../Tools/Scripts:${PATH}"
if [ -n "${WK_ADDITIONAL_SCRIPTS_DIR}" ]; then
    PATH="${SRCROOT}/../../../${WK_ADDITIONAL_SCRIPTS_DIR}:${PATH}"
fi

if [[ -z "$(type -t audit-spi)" && -d "${WK_WORKSPACE_DIR}/Tools/Scripts/libraries/webkitapipy" ]]; then
    # Tools/Scripts/audit-spi and its webkitpy dependencies are not submitted
    # to the production build environment.
    # In their place, call into the webkitapipy python library directly. This
    # bypasses the autoinstaller and does not support incremental build
    # tracking of the sources in WK_ADDITIONAL_SCRIPTS_DIR, so it's only
    # appropriate for production builds where a minimal version of the tool
    # will suffice.
    function audit-spi {
        PYTHONPATH="${WK_WEBKITADDITIONS_HEADERS_FOLDER_PATH}/Scripts:${WK_WORKSPACE_DIR}/Tools/Scripts/libraries/webkitapipy" python3 -m webkitapipy.program $@
    }
fi

# Xcode doesn't expose the name of the discovered dependency file, but by convention, it is
# the same basename as the timestamp output.
depfile="${SCRIPT_OUTPUT_FILE_0/%.timestamp/.d}"

eval FRAMEWORK_SEARCH_PATHS=(${BUILT_PRODUCTS_DIR} ${FRAMEWORK_SEARCH_PATHS} ${SYSTEM_FRAMEWORK_SEARCH_PATHS})
eval LIBRARY_SEARCH_PATHS=(${BUILT_PRODUCTS_DIR} ${LIBRARY_SEARCH_PATHS} ${SYSTEM_LIBRARY_SEARCH_PATHS})
eval WK_AUDIT_SPI_ALLOWLISTS=(${WK_AUDIT_SPI_ALLOWLISTS})

if [[ "${WK_AUDIT_SPI}" == YES && -n "$(type -t audit-spi)" && "${ACTION}" != "installhdrs" && "${ACTION}" != "installapi" ]]; then
    mkdir -p "${OBJROOT}/WebKitSDKDBs"

    if [ "${DEPLOYMENT_LOCATION}" == YES ]; then
        # In install-style builds, the partial SDKDB_DIR is not versioned.
        # We've already selected the appropriate version when populating the
        # SDKROOT.
        versioned_sdkdb_dir="${WK_SDKDB_DIR}"
    else
        # WK_SDKDB_DIR is a directory of directories named according to SDK
        # versions. Pick the versioned directory closest to the active SDK, but not
        # greater. If all available directories are for newer SDKs, fall back to
        # the last one.
        for versioned_sdkdb_dir in $(printf '%s\n' ${WK_SDKDB_DIR}/${PLATFORM_NAME}* | sort -rV); do
            if printf '%s\n' ${versioned_sdkdb_dir#${WK_SDKDB_DIR}/} ${SDK_NAME%.internal} | sort -CV; then
                break
            fi
        done
    fi

    for arch in ${ARCHS}; do
        (set -x && audit-spi $@ \
         --sdkdb-dir "${versioned_sdkdb_dir}" \
         --sdkdb-cache "${OBJROOT}/WebKitSDKDBs/${SDK_NAME}.sqlite3" \
         --sdk-dir "${SDKROOT}" --arch-name "${arch}" \
         --depfile "${depfile}" \
         ${FRAMEWORK_SEARCH_PATHS[@]/#/-F } \
         ${LIBRARY_SEARCH_PATHS[@]/#/-L } \
         ${WK_AUDIT_SPI_ALLOWLISTS[@]/#/--allowlist } \
         ${WK_OTHER_AUDIT_SPI_FLAGS} \
         @"${BUILT_PRODUCTS_DIR}/DerivedSources/${PROJECT_NAME}/platform-enabled-swift-args.${arch}.resp")
     done
else
    [ -n "$(type -t audit-spi)" ] || echo "audit-spi not available, skipping" >&2
    echo "dependencies: " > "${depfile}"
fi
touch "${SCRIPT_OUTPUT_FILE_0}"

# As a side-effect in install-style builds, copy any partial SDKDBs produced
# from building *this* project to the DSTROOT, so that clients of this project
# may incorporate them.
#
# For example, JavaScriptCore's build produces a JavaScriptCore.partial.sdkdb.
# This statement copies it into the DSTROOT at
# /usr/local/lib/SDKDBs/JavaScriptCore.partial.sdkdb. Later, WebCore invokes
# audit-spi and reads this partial SDKDB.
#
# This is a contrived path, only intended for use in tools builds of WebKit,
# not suitable for shipping in the OS.
if [[ "${DEPLOYMENT_LOCATION}" == YES && -d "${WK_SDKDB_OUTPUT_DIR}" ]]; then
    ditto -v "${WK_SDKDB_OUTPUT_DIR}" "${DSTROOT}${WK_LIBRARY_INSTALL_PATH}/SDKDBs"
fi
