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

# Test executables
set(KERN_TESTS test_buf test_str test_dict test_arena test_http_parser test_http_server test_router test_fs_router test_html test_tpl_parser test_tpl_codegen test_tpl_integration)

foreach(test_name ${KERN_TESTS})
    add_executable(${test_name} tests/${test_name}.c)
    target_link_libraries(${test_name} PRIVATE kern)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# test_http_server needs pthreads for the integration test
target_link_libraries(test_http_server PRIVATE pthread)

# Pass fixtures directory to the integration test
target_compile_definitions(test_tpl_integration PRIVATE
    TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
