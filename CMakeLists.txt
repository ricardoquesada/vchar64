cmake_minimum_required(VERSION 3.16)
project(vchar64 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT DEFINED INSTALL_DIR)
    set(INSTALL_DIR "bin")
endif()

add_subdirectory(src)
add_subdirectory(translations)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "${INSTALL_DIR}"
    BUNDLE DESTINATION "${INSTALL_DIR}"
    LIBRARY DESTINATION "${INSTALL_DIR}"
)
