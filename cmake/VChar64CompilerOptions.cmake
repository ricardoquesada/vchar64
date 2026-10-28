# Centralized compiler warning flags, AddressSanitizer/UndefinedBehaviorSanitizer
# instrumentation, and macOS SDK compatibility helpers for VChar64.
#
# All library, application, and test targets link privately against `vchar64_warnings`
# to enforce uniform numeric-conversion, shadowing, and type-safety diagnostics.

include_guard(GLOBAL)

option(VCHAR64_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(vchar64_warnings INTERFACE)
if(MSVC)
    target_compile_options(vchar64_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(vchar64_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wnull-dereference
        -Wformat=2
    )
    if(VCHAR64_ENABLE_SANITIZERS)
        if(APPLE
           AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
           AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "18.0"
           AND CMAKE_HOST_SYSTEM_VERSION VERSION_GREATER_EQUAL "25.0")
            message(WARNING
                "VCHAR64_ENABLE_SANITIZERS=ON with AppleClang ${CMAKE_CXX_COMPILER_VERSION} on Darwin "
                "${CMAKE_HOST_SYSTEM_VERSION} may deadlock in dyld during ASan initialization. "
                "Use AppleClang >= 18 (e.g. -DCMAKE_CXX_COMPILER=/Library/Developer/CommandLineTools/usr/bin/clang++ "
                "-DCMAKE_OSX_SYSROOT=\"$(xcrun --show-sdk-path)\").")
        endif()
        set(_vchar64_sanitizer_compile_flags
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        set(_vchar64_sanitizer_link_flags
            -fsanitize=address,undefined
        )
        # Apply sanitizer flags both to vchar64_warnings and globally so Qt-generated
        # AUTOMOC/AUTORCC translation units are instrumented identically.
        target_compile_options(vchar64_warnings INTERFACE ${_vchar64_sanitizer_compile_flags})
        target_link_options(vchar64_warnings INTERFACE ${_vchar64_sanitizer_link_flags})
        add_compile_options(${_vchar64_sanitizer_compile_flags})
        add_link_options(${_vchar64_sanitizer_link_flags})
    endif()
endif()

# Strips dead macOS AGL.framework tokens injected by upstream Qt / FindWrapOpenGL
# on macOS 14+ / 26+ SDKs using list-element token boundaries (never substring replace).
# Skips ALIASED_TARGET wrappers because CMake forbids mutating properties on alias targets.
function(vchar64_fix_macos_agl)
    if(NOT APPLE)
        return()
    endif()
    foreach(_target WrapOpenGL::WrapOpenGL Qt6::Gui Qt::Gui Qt6::Gui_private Qt::Gui_private)
        if(TARGET ${_target})
            get_target_property(_aliased ${_target} ALIASED_TARGET)
            if(_aliased)
                continue()
            endif()
            get_target_property(_libs ${_target} INTERFACE_LINK_LIBRARIES)
            if(_libs)
                set(_cleaned_libs "")
                set(_skip_next FALSE)
                set(_changed FALSE)
                foreach(_item IN LISTS _libs)
                    if(_skip_next)
                        set(_skip_next FALSE)
                        if(_item STREQUAL "AGL" OR _item MATCHES "(^|/)AGL(\\.framework)?(/AGL)?$")
                            set(_changed TRUE)
                            continue()
                        endif()
                        list(APPEND _cleaned_libs "-framework" "${_item}")
                    elseif(_item STREQUAL "-framework")
                        set(_skip_next TRUE)
                    elseif(_item MATCHES "(^|[/:;, ])(-framework[ ]+)?AGL(\\.framework)?(/AGL)?($|[>; ])")
                        set(_changed TRUE)
                        continue()
                    else()
                        list(APPEND _cleaned_libs "${_item}")
                    endif()
                endforeach()
                if(_skip_next)
                    list(APPEND _cleaned_libs "-framework")
                endif()
                if(_changed)
                    set_target_properties(${_target} PROPERTIES INTERFACE_LINK_LIBRARIES "${_cleaned_libs}")
                endif()
            endif()
        endif()
    endforeach()
endfunction()
