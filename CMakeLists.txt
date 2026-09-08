# -----------------------------------------------------------------------------
# Determine CMake version and build type.
# -----------------------------------------------------------------------------
# NOTE: cmake_minimum_required() and project() *MUST* be the two first commands
# used, see https://cmake.org/cmake/help/v3.3/command/project.html -- the
# latter in particular handles loading a bunch of shared CMake definitions
# and loading the cross-compilation settings from CMAKE_TOOLCHAIN_FILE.

cmake_minimum_required(VERSION 3.20)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/Source/cmake")

if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    # Resolve the SDK and pin tools before the call to project().
    include(WebKitXcodeSDK)
endif ()

project(WebKit LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# Common configuration
#------------------------------------------------------------------------------
include(WebKitCommon)

# -----------------------------------------------------------------------------
# Enable Swift - we can't do this until we've checked our configuration,
# which in turn depends on clang-related variables set by the initial
# "project()" line
#------------------------------------------------------------------------------
if (SWIFT_REQUIRED)
    if (POLICY CMP0157)
        cmake_policy(SET CMP0157 NEW)
    endif ()

    enable_language(Swift)

    # -F causes Swift to find C++23 framework headers in implicit module builds.
    if (WEBKIT_SDK_IS_IOS_FAMILY)
        set(CMAKE_Swift_FRAMEWORK_SEARCH_FLAG "-Xcc -iquote")
    endif ()

    # CMAKE_Swift_LAUNCHER does not work, so we need to manually set our
    # wrapper as the compiler program.
    set(ORIGINAL_Swift_COMPILER "${CMAKE_Swift_COMPILER}")
    if (WIN32)
        # Avoid cmd.exe due to command-line length limits.
        set(CMAKE_Swift_COMPILER "${Python_EXECUTABLE}")
        set(CMAKE_Swift_COMPILER_ARG1 "${CMAKE_SOURCE_DIR}/Tools/Scripts/swift/swiftc-wrapper.py")
    else ()
        set(CMAKE_Swift_COMPILER "${CMAKE_SOURCE_DIR}/Tools/Scripts/swift/swiftc-wrapper.py")
    endif ()
    add_compile_options($<$<COMPILE_LANGUAGE:Swift>:--original-swift-compiler=${ORIGINAL_Swift_COMPILER}>)
    add_link_options($<$<LINK_LANGUAGE:Swift>:--original-swift-compiler=${ORIGINAL_Swift_COMPILER}>)

    if (APPLE)
        # When Swift is used as a linker frontend, disable dSYM generation. The
        # objects being linked still contain debug info.
        add_link_options("$<$<LINK_LANGUAGE:Swift>:-gnone>")
    endif ()
    # The static archive rule (<CMAKE_Swift_CREATE_STATIC_LIBRARY>) uses neither
    # <FLAGS> nor link options, so inject the flag directly into the template.
    string(REPLACE "<CMAKE_Swift_COMPILER>"
        "<CMAKE_Swift_COMPILER> --original-swift-compiler=${ORIGINAL_Swift_COMPILER}"
        CMAKE_Swift_CREATE_STATIC_LIBRARY "${CMAKE_Swift_CREATE_STATIC_LIBRARY}")

    # Strip parallelism flags from the swiftc invocations that CMake has
    # computed, so that our jobserver logic in `ninja-wrapper` takes
    # precedence.
    foreach (_rule COMPILE_OBJECT CREATE_SHARED_LIBRARY CREATE_SHARED_MODULE
            CREATE_STATIC_LIBRARY LINK_EXECUTABLE LINK_EXECUTABLE_WITH_EXPORTS)
        string(REGEX REPLACE " -j [0-9]+ -num-threads [0-9]+" ""
            CMAKE_Swift_${_rule} "${CMAKE_Swift_${_rule}}")
        if (CMAKE_Swift_${_rule} MATCHES "-num-threads")
            message(WARNING "CMAKE_Swift_${_rule} still sets Swift's own "
                "parallelism, so the driver will not follow the jobserver: "
                "${CMAKE_Swift_${_rule}}")
        endif ()
    endforeach ()
endif ()

# -----------------------------------------------------------------------------
# Enable API unit tests and create a target for the test runner
# -----------------------------------------------------------------------------
if (ENABLE_API_TESTS)
    enable_testing()
endif ()

# -----------------------------------------------------------------------------
# Add module directories
# -----------------------------------------------------------------------------
add_subdirectory(Source)

# -----------------------------------------------------------------------------
# Add tools
# -----------------------------------------------------------------------------
if (ENABLE_TOOLS)
    add_subdirectory(Tools)
endif ()

if (DEVELOPER_MODE)
    if (EXISTS "${CMAKE_SOURCE_DIR}/PerformanceTests")
        add_subdirectory(PerformanceTests)
    endif ()
endif ()

# -----------------------------------------------------------------------------
# Print the features list last, for maximum visibility.
# -----------------------------------------------------------------------------
PRINT_WEBKIT_OPTIONS()
