cmake_minimum_required(VERSION 3.16)

# Re-use the same project name or a new one
project(vchar64_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Widgets Network)

# We need to compile the sources from src/ but we cannot easily link against an executable target
# unless we exported objects. For simplicity, we'll add the necessary sources here.
# We exclude main.cpp to avoid multiple entry points.

set(VCHAR64_SRC_DIR "${CMAKE_SOURCE_DIR}/src")

set(TEST_SOURCES
    src/tst_stateimport.cpp
    ${VCHAR64_SRC_DIR}/stateimport.cpp
    
    # Stubs
    src/stateexport_stub.cpp
)

# Resources
set(TEST_RESOURCES
    ${VCHAR64_SRC_DIR}/resources.qrc
)

qt_add_executable(vchar64_test
    ${TEST_SOURCES}
    ${TEST_RESOURCES}
)

target_include_directories(vchar64_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VCHAR64_SRC_DIR}
)
target_compile_definitions(vchar64_test PRIVATE 
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    VERSION="0.2.5"
    GIT_VERSION="n/a"
    UNIT_TEST
)

target_link_libraries(vchar64_test PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Test
    Qt::Widgets
    Qt::Network
)

add_test(NAME vchar64_test COMMAND vchar64_test)
