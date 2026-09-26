# Tests support

# If we're compiling with GCC on BSD systems, build GTest from source instead of
# using the system-provided package because it's typically built with the base
# Clang/libc++ toolchain, which fails to link (detected on GhostBSD + GCC 17.x).

if (DOSBOX_IS_BSD_GCC)
    message(STATUS "BSD + GCC detected: vendoring GoogleTest to match compiler ABI")

    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz
        URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    )
    set(INSTALL_GTEST OFF)
    set(BUILD_GMOCK ON)
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(googletest)
    unset(BUILD_SHARED_LIBS)

else()
    find_package(GTest REQUIRED)
endif()

add_executable(dosbox_tests
    ansi_code_markup_tests.cpp
    batch_file_tests.cpp
    bda_cursor_type_tests.cpp
    bit_view_tests.cpp
    bitops_tests.cpp
    cmd_move_tests.cpp
    command_line_tests.cpp
    config_help_tests.cpp
    dos_files_tests.cpp
    dos_io_delay_tests.cpp
    dos_keyboard_layout_setting_tests.cpp
    dos_memory_struct_tests.cpp
    dos_psp_environment_tests.cpp
    dos_runtime_settings_tests.cpp
    dosbox_test_fixture.h
    drive_swap_tests.cpp
    drives_tests.cpp
    fraction_tests.cpp
    fs_utils_tests.cpp
    fs_utf8_shortname_tests.cpp
    i8042_wide_read_tests.cpp
    int10_modes_tests.cpp
    joystick_reinit_tests.cpp
    language_territory_tests.cpp
    math_utils_tests.cpp
    messages_adjust_tests.cpp
    mixer_tests.cpp
    mode_symbolic_tests.cpp
    mount_tests.cpp
    ne2000_tests.cpp
    path_command_tests.cpp
    port_containers_tests.cpp
    program_append_tests.cpp
    program_join_tests.cpp
    program_mixer_tests.cpp
    rect_tests.cpp
    rgb_tests.cpp
    ring_buffer_tests.cpp
    rwqueue_tests.cpp
    shader_pragma_parser_tests.cpp
    shell_banner_tests.cpp
    shell_cmds_tests.cpp
    shell_redirection_tests.cpp
    string_utils_tests.cpp
    # stubs.cpp
    support_tests.cpp
    test_temp_dir.h
    test_temp_dir_tests.cpp
    truetype_character_block_tests.cpp
    truetype_tests.cpp
    unicode_tests.cpp
    webserver_auth_tests.cpp
    webserver_tools_tests.cpp
    webserver_tool_launch_tests.cpp
    host_browser_tests.cpp
    host_layout_table_tests.cpp
    docs_location_tests.cpp
    osd_tests.cpp
    mount_policy_tests.cpp
    lua_engine_tests.cpp
    lua_coroutine_tests.cpp
    lua_api_tests.cpp
    script_validator_tests.cpp
    text_input_tests.cpp
    webserver_mem_search_tests.cpp
    webserver_freeze_tests.cpp
    webserver_dos_map_tests.cpp
    webserver_io_port_tests.cpp
    webserver_cpu_write_tests.cpp
    webserver_frame_tap_tests.cpp
    multi_prefix_tests.cpp
    zmbv_tests.cpp
    render_scale_tests.cpp
    truetype_freetype_tests.cpp
    cross_tests.cpp
)

# Disable some warnings for deliberately flawed test cases
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(dosbox_tests PRIVATE
        "-Wno-effc++"
        "-Wno-gnu-zero-variadic-macro-arguments"
    )
    # Clang does not support -Wno-format-overflow
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(dosbox_tests PRIVATE
            "-Wno-format-overflow"
        )
    endif()
endif()

target_link_libraries(dosbox_tests PRIVATE
    GTest::gmock_main
    dosboxcommon
    lua
    nlohmann_json::nlohmann_json
    SDL3::Headers
    Freetype::Freetype
    ${ZMBV_ZLIB_TARGET}
)

if (DOSBOX_IS_BSD_GCC)
    target_link_options(dosbox_tests PRIVATE -static-libstdc++ -static-libgcc)
endif()

# %TEMP% is on the Windows mount denylist, so the mount suites keep their
# fixtures in the build tree there (ada-925x)
if (WIN32)
    target_compile_definitions(dosbox_tests PRIVATE
        DOSBOX_TEST_SCRATCH_DIR="${CMAKE_BINARY_DIR}/test-scratch"
    )
endif()

include(GoogleTest)

# `DISCOVERY_MODE PRE_TEST` is needed for local builds on macOS for test
# discovery to work, otherwise the build will just fail with an error.
#
# Probably related to:
# https://gitlab.kitware.com/cmake/cmake/-/issues/21845
#
gtest_discover_tests(
  dosbox_tests WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  DISCOVERY_MODE PRE_TEST
  DISCOVERY_TIMEOUT 60
)
