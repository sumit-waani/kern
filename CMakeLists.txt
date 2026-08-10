cmake_minimum_required(VERSION 3.16)
project(kern C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Compiler flags
add_compile_options(-pedantic -Wall -Wextra)
# libuv headers on Linux require _GNU_SOURCE for pthread types
add_compile_definitions(_GNU_SOURCE)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUV REQUIRED libuv)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# libkern static library
add_library(kern STATIC
    src/kern_buf.c
    src/kern_str.c
    src/kern_dict.c
    src/kern_arena.c
    src/kern_http_parser.c
    src/kern_request.c
    src/kern_response.c
    src/kern_http_server.c
    src/kern_static.c
    src/kern_router.c
    src/kern_middleware.c
    src/kern_fs_router.c
    src/kern_html.c
    src/kern_tpl_parser.c
    src/kern_tpl_codegen.c
    src/kern_tpl_compile.c
    src/kern_db.c
    src/kern_qb.c
    src/kern_migrate.c
    src/kern_config.c
    src/kern_session.c
    src/kern_auth.c
    src/kern_asset.c
    src/kern_fmt.c
    src/kern_test.c
    src/kern_shard.c
    src/kern_shard_static.c
    src/kern_tailwind.c
    src/kern_app.c
)

target_include_directories(kern PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${LIBUV_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(kern PUBLIC
    ${LIBUV_LIBRARIES}
    ${SQLITE3_LIBRARIES}
)

target_link_directories(kern PUBLIC
    ${LIBUV_LIBRARY_DIRS}
    ${SQLITE3_LIBRARY_DIRS}
)

# Enable testing
enable_testing()

# kern CLI executable
add_executable(kern_cli
    cli/main.c
    cli/cmd_new.c
    cli/cmd_build.c
    cli/cmd_dev.c
    cli/cmd_fmt.c
    cli/cmd_test.c
)
set_target_properties(kern_cli PROPERTIES OUTPUT_NAME "kern")
target_link_libraries(kern_cli PRIVATE kern)

# Test executables
set(KERN_TESTS test_buf test_str test_dict test_arena test_http_parser test_http_server test_router test_fs_router test_html test_tpl_parser test_tpl_codegen test_tpl_integration test_db test_qb test_migrate test_config test_session test_auth test_app test_asset test_static test_fmt test_test_framework test_shard test_tailwind)

foreach(test_name ${KERN_TESTS})
    add_executable(${test_name} tests/${test_name}.c)
    target_link_libraries(${test_name} PRIVATE kern)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# test_http_server needs pthreads for the integration test
target_link_libraries(test_http_server PRIVATE pthread)

# kern_shard_static.c uses an overlength string literal for embedded JS
set_source_files_properties(src/kern_shard_static.c PROPERTIES
    COMPILE_FLAGS "-Wno-overlength-strings"
)

# Pass fixtures directory to the integration test
target_compile_definitions(test_tpl_integration PRIVATE
    TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
