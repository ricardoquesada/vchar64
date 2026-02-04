cmake_minimum_required(VERSION 3.16)

# Re-use the same project name or a new one
project(vchar64_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Widgets Network)
set(CMAKE_AUTOMOC ON)

# We need to compile the sources from src/ but we cannot easily link against an executable target
# unless we exported objects. For simplicity, we'll add the necessary sources here.
# We exclude main.cpp to avoid multiple entry points.

set(VCHAR64_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src")

# Common sources
set(COMMON_SOURCES
    # Dependencies
    ${VCHAR64_SRC_DIR}/importkoalabitmapwidget.cpp
    ${VCHAR64_SRC_DIR}/palette.cpp
    # Resources
    ${VCHAR64_SRC_DIR}/resources.qrc
)

# Test StateImport
qt_add_executable(test_stateimport
    src/tst_stateimport.cpp
    ${VCHAR64_SRC_DIR}/stateimport.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_stateimport PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VCHAR64_SRC_DIR}
)

target_compile_definitions(test_stateimport PRIVATE 
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    VERSION="0.99.0"
    GIT_VERSION="n/a"
    UNIT_TEST
)

target_link_libraries(test_stateimport PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Test
    Qt::Widgets
    Qt::Network
)

add_test(NAME test_stateimport COMMAND test_stateimport)

# Test StateExport
qt_add_executable(test_stateexport
    src/tst_stateexport.cpp
    ${VCHAR64_SRC_DIR}/stateexport.cpp
    ${COMMON_SOURCES}
)

target_include_directories(test_stateexport PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VCHAR64_SRC_DIR}
)

target_compile_definitions(test_stateexport PRIVATE 
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    VERSION="0.99.0"
    GIT_VERSION="n/a"
    UNIT_TEST
)

target_link_libraries(test_stateexport PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Test
    Qt::Widgets
    Qt::Network
)

add_test(NAME test_stateexport COMMAND test_stateexport)
