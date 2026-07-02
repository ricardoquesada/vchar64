cmake_minimum_required(VERSION 3.16)
project(vchar64 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT DEFINED INSTALL_DIR)
    set(INSTALL_DIR "bin")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)

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
qt_standard_project_setup(
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES es
)

qt_add_translations(${PROJECT_NAME}
    TS_FILES translations/vchar64_es.ts
    RESOURCE_PREFIX "/i18n"
)

add_subdirectory(src)
# add_subdirectory(translations)

enable_testing()
add_subdirectory(tests/unit_tests)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "${INSTALL_DIR}"
    BUNDLE DESTINATION "${INSTALL_DIR}"
    LIBRARY DESTINATION "${INSTALL_DIR}"
)
