# Headless CTest suite definitions for VChar64.
#
# Every unit test executable uses QTEST_GUILESS_MAIN (QCoreApplication) and links
# strictly against `vchar64_core` or `vchar64_net` with zero Qt6::Widgets dependency.
# This guarantees compile-time failure if UI symbols leak into headless libraries
# and allows tests to run in CI without X11, Wayland, or xvfb-run.

function(vchar64_add_core_unit_test target_name source_file)
    qt_add_executable(${target_name}
        ${source_file}
    )

    target_compile_definitions(${target_name} PRIVATE
        SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    target_link_libraries(${target_name} PRIVATE
        vchar64_core
        Qt6::Test
        vchar64_warnings
    )

    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

vchar64_add_core_unit_test(tst_state_and_commands src/tst_state_and_commands.cpp)
vchar64_add_core_unit_test(tst_stateimport src/tst_stateimport.cpp)
vchar64_add_core_unit_test(tst_stateexport src/tst_stateexport.cpp)

# Links vchar64_net (transitively pulling vchar64_core + Qt6::Network) to test
# ClipboardCodec, C64 screen/color builders, async ServerPreview, and AutoUpdater.
qt_add_executable(tst_clipboard_and_network
    src/tst_clipboard_and_network.cpp
)

target_compile_definitions(tst_clipboard_and_network PRIVATE
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(tst_clipboard_and_network PRIVATE
    vchar64_net
    Qt6::Test
    vchar64_warnings
)

add_test(NAME tst_clipboard_and_network COMMAND tst_clipboard_and_network)
