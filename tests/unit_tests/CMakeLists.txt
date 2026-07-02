cmake_minimum_required(VERSION 3.16)

# Re-use the same project name or a new one
project(vchar64_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Widgets Network)

# Fix macOS CI build failure due to AGL framework removal in macOS 14+ SDK
if(APPLE)
    foreach(_target WrapOpenGL::WrapOpenGL Qt6::Gui Qt::Gui Qt6::Gui_private Qt::Gui_private)
        if(TARGET ${_target})
            get_target_property(_libs ${_target} INTERFACE_LINK_LIBRARIES)
            if(_libs)
                set(_new_libs "")
                set(_changed FALSE)
                foreach(_lib ${_libs})
                    if(_lib MATCHES "AGL" OR _lib STREQUAL "AGL")
                        set(_changed TRUE)
                    else()
                        list(APPEND _new_libs "${_lib}")
                    endif()
                endforeach()
                if(_changed)
                    set_target_properties(${_target} PROPERTIES INTERFACE_LINK_LIBRARIES "${_new_libs}")
                endif()
            endif()
        endif()
    endforeach()
endif()
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
