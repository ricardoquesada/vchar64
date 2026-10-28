# Top-level CMake build configuration for VChar64.
#
# Enforces strict ISO C++20 across all library, executable, and unit-test targets,
# separates semantic version strings from optional Git commit hashes, and registers
# multi-target Qt Linguist translation extraction after target creation.

cmake_minimum_required(VERSION 3.16)
project(vchar64 VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTORCC ON)

if(NOT DEFINED INSTALL_DIR)
    set(INSTALL_DIR "bin")
endif()

include(cmake/VChar64CompilerOptions.cmake)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets LinguistTools Test)
# Strip removed macOS AGL.framework link tokens injected by upstream Qt6::Gui / WrapOpenGL.
vchar64_fix_macos_agl()

qt_standard_project_setup(
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES es
)

# Resolve the short Git commit hash separately from PROJECT_VERSION so
# QCoreApplication::applicationVersion() remains a strict QVersionNumber-compatible semver.
set(VCHAR64_RESOLVED_GIT_HASH "")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE VCHAR64_GIT_RESULT
        OUTPUT_VARIABLE VCHAR64_RESOLVED_GIT_HASH
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT VCHAR64_GIT_RESULT EQUAL 0)
        set(VCHAR64_RESOLVED_GIT_HASH "")
    endif()
endif()

add_compile_definitions(
    VCHAR64_VERSION="${PROJECT_VERSION}"
    VCHAR64_GIT_HASH="${VCHAR64_RESOLVED_GIT_HASH}"
)

add_subdirectory(src)

# Must run after add_subdirectory(src) so lupdate scans tr() and QT_TR_NOOP()
# across all three layered targets (vchar64_core, vchar64_net, and vchar64).
qt_add_translations(${PROJECT_NAME}
    TS_FILES translations/vchar64_es.ts
    RESOURCE_PREFIX "/i18n"
    SOURCE_TARGETS vchar64_core vchar64_net vchar64
)

enable_testing()
add_subdirectory(tests/unit_tests)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "${INSTALL_DIR}"
    BUNDLE DESTINATION "${INSTALL_DIR}"
    LIBRARY DESTINATION "${INSTALL_DIR}"
)
