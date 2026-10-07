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
target_include_directories(preview_proxy PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(preview_proxy PRIVATE video_trim_ui Qt5::Core Qt5::Gui Qt5::Widgets)
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
endif()
