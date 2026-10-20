cmake_minimum_required(VERSION 3.21)

project(BillfishVideoTrimPatch VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Billfish preview proxy is supported on Windows only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(CTest)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Network Widgets)

# CONFIGURE_DEPENDS keeps the standalone patch build extensible as core/UI files
# are added without requiring a hand-maintained source list.
file(GLOB_RECURSE VIDEO_TRIM_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h"
)
file(GLOB_RECURSE VIDEO_TRIM_UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h"
)
file(GLOB_RECURSE VIDEO_TRIM_PROXY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/*.def"
)

add_library(video_trim_core STATIC ${VIDEO_TRIM_CORE_SOURCES})
target_include_directories(video_trim_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
)
target_link_libraries(video_trim_core PUBLIC Qt5::Core)
target_link_libraries(video_trim_core PUBLIC User32)
target_compile_definitions(video_trim_core PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

add_library(video_trim_ui STATIC ${VIDEO_TRIM_UI_SOURCES})
target_include_directories(video_trim_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(video_trim_ui PUBLIC
    video_trim_core
    Qt5::Core
    Qt5::Gui
    Qt5::Network
    Qt5::Widgets
)
target_compile_definitions(video_trim_ui PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

add_library(preview_proxy SHARED ${VIDEO_TRIM_PROXY_SOURCES})
add_library(billfish_shell STATIC src/shell/compact_shell.cpp src/shell/compact_shell.hpp
    src/shell/legacy_feature_gate.cpp src/shell/legacy_feature_gate.hpp
    src/shell/eagle_toolbar.cpp src/shell/eagle_toolbar.hpp)
target_include_directories(billfish_shell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(billfish_shell PUBLIC Qt5::Core Qt5::Widgets)
target_compile_definitions(billfish_shell PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_compile_definitions(billfish_shell PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(billfish_shell PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
endif()
file(GLOB_RECURSE BILLFISH_AUTOMATION_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/automation/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/automation/*.hpp"
)
add_library(billfish_automation STATIC ${BILLFISH_AUTOMATION_SOURCES})
target_include_directories(billfish_automation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(billfish_automation PUBLIC Qt5::Core Qt5::Gui Qt5::Network Qt5::Widgets)
target_compile_definitions(billfish_automation PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_compile_definitions(billfish_automation PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
if(MSVC)
    target_compile_options(billfish_automation PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
endif()
target_include_directories(preview_proxy PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(preview_proxy PRIVATE video_trim_ui billfish_automation billfish_shell Qt5::Core Qt5::Gui Qt5::Widgets)
target_compile_definitions(preview_proxy PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
set_target_properties(preview_proxy PROPERTIES
    OUTPUT_NAME "preview"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(preview_proxy PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/bin"
        "LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/bin"
        "ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/lib"
    )
endforeach()

if(MSVC)
    target_compile_options(video_trim_core PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    target_compile_options(video_trim_ui PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    target_compile_options(preview_proxy PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
endif()

if(BUILD_TESTING)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Test)
    get_filename_component(VIDEO_TRIM_QT_RUNTIME_ROOT "${Qt5_DIR}/../../.." ABSOLUTE)
    set(VIDEO_TRIM_QT_PLATFORM_PLUGIN_DIR
        "${VIDEO_TRIM_QT_RUNTIME_ROOT}/plugins/platforms")

    function(stage_qt_test_runtime target_name)
        foreach(qt_runtime_target
                Qt5::Core Qt5::Gui Qt5::Network Qt5::Test Qt5::Widgets)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:${qt_runtime_target}>"
                    "$<TARGET_FILE_DIR:${target_name}>"
                VERBATIM
            )
        endforeach()
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:${target_name}>/platforms"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${VIDEO_TRIM_QT_PLATFORM_PLUGIN_DIR}/qoffscreen.dll"
                "$<TARGET_FILE_DIR:${target_name}>/platforms/qoffscreen.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${VIDEO_TRIM_QT_PLATFORM_PLUGIN_DIR}/qwindows.dll"
                "$<TARGET_FILE_DIR:${target_name}>/platforms/qwindows.dll"
            VERBATIM
        )
    endfunction()
    file(GLOB_RECURSE VIDEO_TRIM_CORE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/*.h"
    )
    if(VIDEO_TRIM_CORE_TEST_SOURCES)
        add_executable(video_trim_core_tests ${VIDEO_TRIM_CORE_TEST_SOURCES})
        target_link_libraries(video_trim_core_tests PRIVATE video_trim_core Shell32 Ole32)
        if(MSVC)
            target_compile_options(video_trim_core_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
        endif()
        set_target_properties(video_trim_core_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        )
        foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER "${configuration}" configuration_upper)
            set_target_properties(video_trim_core_tests PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/tests"
            )
        endforeach()
        add_test(NAME video_trim_core_tests COMMAND video_trim_core_tests)
        set_tests_properties(video_trim_core_tests PROPERTIES TIMEOUT 60)
        stage_qt_test_runtime(video_trim_core_tests)
    endif()

    set(MPV_BACKEND_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tests/mpv_process_backend")
    add_executable(fake_mpv "${MPV_BACKEND_TEST_ROOT}/fake_mpv.cpp")
    add_executable(mpv_process_backend_tests
        "${MPV_BACKEND_TEST_ROOT}/test_mpv_process_backend.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/mpv_process_backend.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/mpv_process_backend.hpp"
    )
    target_include_directories(mpv_process_backend_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(mpv_process_backend_tests PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Network
        Qt5::Test
        Qt5::Widgets
    )
    get_filename_component(VIDEO_TRIM_BILLFISH_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    file(TO_CMAKE_PATH "${VIDEO_TRIM_BILLFISH_ROOT}"
        VIDEO_TRIM_BILLFISH_TEST_ROOT)
    target_compile_definitions(mpv_process_backend_tests PRIVATE
        FAKE_MPV_PATH="$<TARGET_FILE:fake_mpv>"
        BILLFISH_TEST_ROOT="${VIDEO_TRIM_BILLFISH_TEST_ROOT}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
    )
    add_dependencies(mpv_process_backend_tests fake_mpv)
    stage_qt_test_runtime(mpv_process_backend_tests)
    if(MSVC)
        target_compile_options(fake_mpv PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
        target_compile_options(mpv_process_backend_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
    foreach(test_target fake_mpv mpv_process_backend_tests)
        set_target_properties(${test_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        )
        foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER "${configuration}" configuration_upper)
            set_target_properties(${test_target} PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/tests"
            )
        endforeach()
    endforeach()
    add_test(NAME mpv_process_backend_tests COMMAND mpv_process_backend_tests -platform offscreen)
    set_tests_properties(mpv_process_backend_tests PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${VIDEO_TRIM_QT_PLATFORM_PLUGIN_DIR}"
        TIMEOUT 60
    )

    file(GLOB_RECURSE VIDEO_TRIM_UI_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/*.h"
    )
    set(STOCK_PREVIEW_FIXTURE_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/stock_preview_fixture.cpp")
    list(REMOVE_ITEM VIDEO_TRIM_UI_TEST_SOURCES "${STOCK_PREVIEW_FIXTURE_SOURCE}")

    add_library(stock_preview_fixture MODULE "${STOCK_PREVIEW_FIXTURE_SOURCE}")
    target_link_libraries(stock_preview_fixture PRIVATE Qt5::Core)
    set_target_properties(stock_preview_fixture PROPERTIES
        OUTPUT_NAME "preview.stock"
        PREFIX ""
        SUFFIX ".bin"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(stock_preview_fixture PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/bin"
            "LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/bin"
        )
    endforeach()

    add_executable(video_trim_ui_tests ${VIDEO_TRIM_UI_TEST_SOURCES})
    target_include_directories(video_trim_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(video_trim_ui_tests PRIVATE video_trim_ui Qt5::Test Qt5::Widgets)
    file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/bin/preview.dll" PREVIEW_PROXY_TEST_PATH)
    file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/tests/video_trim_core_tests.exe" CORE_TEST_TOOL_PATH)
    target_compile_definitions(video_trim_ui_tests PRIVATE
        PREVIEW_PROXY_PATH="${PREVIEW_PROXY_TEST_PATH}"
        CORE_TEST_TOOL_PATH="${CORE_TEST_TOOL_PATH}"
        BILLFISH_TEST_ROOT="${VIDEO_TRIM_BILLFISH_TEST_ROOT}")
    add_dependencies(video_trim_ui_tests preview_proxy stock_preview_fixture)
    stage_qt_test_runtime(video_trim_ui_tests)
    if(TARGET video_trim_core_tests)
        add_dependencies(video_trim_ui_tests video_trim_core_tests)
    endif()
    if(MSVC)
        target_compile_options(video_trim_ui_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
    set_target_properties(video_trim_ui_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )
    foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(video_trim_ui_tests PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/tests"
        )
    endforeach()
    add_test(NAME video_trim_ui_tests COMMAND video_trim_ui_tests -platform offscreen)
    set_tests_properties(video_trim_ui_tests PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${VIDEO_TRIM_QT_PLATFORM_PLUGIN_DIR}"
        TIMEOUT 90
    )

    add_executable(test_app_control tests/automation/test_app_control.cpp
        src/automation/app_control.cpp src/automation/app_control.hpp src/automation/app_input_scope.hpp)
    target_include_directories(test_app_control PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(test_app_control PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test)
    target_compile_definitions(test_app_control PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
    if(MSVC)
        target_compile_options(test_app_control PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
    set_target_properties(test_app_control PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(test_app_control PROPERTIES "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/tests")
    endforeach()
    stage_qt_test_runtime(test_app_control)
    add_test(NAME test_app_control COMMAND test_app_control -platform offscreen)
    set_tests_properties(test_app_control PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${VIDEO_TRIM_QT_PLATFORM_PLUGIN_DIR}" TIMEOUT 60)
    add_executable(billfish_plugin_tests tests/automation/test_plugin_manager.cpp)
    add_library(billfish_animation_visibility_probe SHARED diagnostics/window_probe/animation_visibility_probe.cpp)
    target_link_libraries(billfish_animation_visibility_probe PRIVATE billfish_automation Qt5::Widgets)
    target_compile_definitions(billfish_animation_visibility_probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_target_properties(billfish_animation_visibility_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/diagnostics")
    if(MSVC)
        target_compile_options(billfish_animation_visibility_probe PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
    add_library(billfish_animation_cover_probe SHARED diagnostics/window_probe/animation_cover_probe.cpp)
    target_link_libraries(billfish_animation_cover_probe PRIVATE billfish_automation Qt5::Widgets)
    target_compile_definitions(billfish_animation_cover_probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_target_properties(billfish_animation_cover_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/diagnostics"
        OUTPUT_NAME "billfish_animation_cover_probe_v2")
    if(MSVC)
        target_compile_options(billfish_animation_cover_probe PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
    add_library(billfish_animation_filter_probe SHARED diagnostics/window_probe/animation_filter_probe.cpp)
    target_link_libraries(billfish_animation_filter_probe PRIVATE billfish_automation Qt5::Widgets)
    target_compile_definitions(billfish_animation_filter_probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_target_properties(billfish_animation_filter_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/diagnostics")
    if(MSVC)
        target_compile_options(billfish_animation_filter_probe PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
    add_library(billfish_animation_cover_fetch_probe SHARED diagnostics/window_probe/animation_cover_fetch_probe.cpp)
    target_link_libraries(billfish_animation_cover_fetch_probe PRIVATE Qt5::Core Qt5::Gui)
    target_compile_definitions(billfish_animation_cover_fetch_probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_target_properties(billfish_animation_cover_fetch_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/diagnostics"
        OUTPUT_NAME "billfish_animation_cover_probe_v5")
    if(MSVC)
        target_compile_options(billfish_animation_cover_fetch_probe PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME billfish_plugin_package_tests COMMAND "${Python3_EXECUTABLE}" -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}/../automation/plugin_runtime/tests" -p test_package_installer.py)
        set_tests_properties(billfish_plugin_package_tests PROPERTIES TIMEOUT 30)
    endif()
    add_executable(billfish_automation_ui_tests tests/automation/test_automation_ui.cpp)
    add_executable(billfish_video_capture_tests tests/automation/test_video_capture.cpp)
    add_executable(billfish_media_transfer_tests tests/automation/test_media_transfer_process.cpp)
    foreach(automation_test billfish_plugin_tests billfish_automation_ui_tests billfish_video_capture_tests billfish_media_transfer_tests)
        target_link_libraries(${automation_test} PRIVATE billfish_automation Qt5::Test Qt5::Widgets)
        if(MSVC)
            target_compile_options(${automation_test} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
        endif()
        set_target_properties(${automation_test} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER "${configuration}" configuration_upper)
            set_target_properties(${automation_test} PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/tests")
        endforeach()
        stage_qt_test_runtime(${automation_test})
        if(NOT automation_test STREQUAL "billfish_plugin_tests" AND NOT automation_test STREQUAL "billfish_media_transfer_tests")
            add_test(NAME ${automation_test} COMMAND ${automation_test} -platform offscreen)
        else()
            add_test(NAME ${automation_test} COMMAND ${automation_test})
        endif()
        set_tests_properties(${automation_test} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${VIDEO_TRIM_QT_PLATFORM_PLUGIN_DIR}"
            TIMEOUT 90)
    endforeach()
    set_property(TEST video_trim_ui_tests APPEND PROPERTY ENVIRONMENT
        "APPDATA=${CMAKE_BINARY_DIR}/test-profile/appdata"
        "LOCALAPPDATA=${CMAKE_BINARY_DIR}/test-profile/localappdata")

    add_executable(billfish_shell_tests tests/shell/test_compact_shell.cpp)
    add_executable(billfish_legacy_gate_tests tests/shell/test_legacy_feature_gate.cpp)
    target_link_libraries(billfish_legacy_gate_tests PRIVATE billfish_shell Qt5::Test Qt5::Widgets)
    if(MSVC)
        target_compile_options(billfish_legacy_gate_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
    set_target_properties(billfish_legacy_gate_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(billfish_legacy_gate_tests PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/tests")
    endforeach()
    stage_qt_test_runtime(billfish_legacy_gate_tests)
    add_test(NAME billfish_legacy_gate_tests COMMAND billfish_legacy_gate_tests -platform offscreen)
    set_tests_properties(billfish_legacy_gate_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${VIDEO_TRIM_QT_PLATFORM_PLUGIN_DIR}"
        TIMEOUT 60)
    target_link_libraries(billfish_shell_tests PRIVATE billfish_shell Qt5::Test Qt5::Widgets)
    if(MSVC)
        target_compile_options(billfish_shell_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
    set_target_properties(billfish_shell_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(billfish_shell_tests PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/tests")
    endforeach()
    stage_qt_test_runtime(billfish_shell_tests)
    add_test(NAME billfish_shell_tests COMMAND billfish_shell_tests -platform offscreen)
    set_tests_properties(billfish_shell_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${VIDEO_TRIM_QT_PLATFORM_PLUGIN_DIR}"
        TIMEOUT 60)
    add_executable(billfish_eagle_toolbar_tests tests/shell/test_eagle_toolbar.cpp)
    target_link_libraries(billfish_eagle_toolbar_tests PRIVATE billfish_shell Qt5::Test Qt5::Widgets)
    if(MSVC)
        target_compile_options(billfish_eagle_toolbar_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
    set_target_properties(billfish_eagle_toolbar_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(billfish_eagle_toolbar_tests PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/tests")
    endforeach()
    stage_qt_test_runtime(billfish_eagle_toolbar_tests)
    add_test(NAME billfish_eagle_toolbar_tests COMMAND billfish_eagle_toolbar_tests -platform offscreen)
    set_tests_properties(billfish_eagle_toolbar_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${VIDEO_TRIM_QT_PLATFORM_PLUGIN_DIR}"
        TIMEOUT 60)
endif()
