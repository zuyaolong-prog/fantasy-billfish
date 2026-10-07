cmake_minimum_required(VERSION 3.22)

project(BillfishClone
    VERSION 0.1.0
    DESCRIPTION "Clean-room Billfish clone (Windows coexist version)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(bfcore
    src/core/api/types.cpp
    src/core/db/sqlite_runtime.cpp
    src/core/db/database.cpp
    src/core/db/schema.cpp
    src/core/util/fs_utils.cpp
    src/core/util/text_utils.cpp
    src/core/datacenter/billfish_datacenter.cpp)

target_include_directories(bfcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(bfcli
    src/app/main.cpp)

target_link_libraries(bfcli PRIVATE bfcore)

install(TARGETS bfcore bfcli)
