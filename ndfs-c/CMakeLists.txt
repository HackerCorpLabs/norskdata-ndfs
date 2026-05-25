cmake_minimum_required(VERSION 3.10)
project(ndfs C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ── Library ──────────────────────────────────────────────────────────

set(NDFS_SOURCES
    src/endian_util.c
    src/ndfs_name.c
    src/block_pointer.c
    src/master_block.c
    src/bit_file.c
    src/user_friend.c
    src/user_entry.c
    src/object_entry.c
    src/filesystem.c
    src/image_creator.c
    src/boot_loader.c
    src/xat.c
    src/parity.c
    src/wildmatch.c
)

add_library(ndfs STATIC ${NDFS_SOURCES})

target_include_directories(ndfs
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Strict warnings
if(MSVC)
    target_compile_options(ndfs PRIVATE /W4)
else()
    target_compile_options(ndfs PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Tests ────────────────────────────────────────────────────────────

enable_testing()

set(TEST_SOURCES
    tests/test_main.c
    tests/test_endian.c
    tests/test_block_pointer.c
    tests/test_master_block.c
    tests/test_bit_file.c
    tests/test_user_entry.c
    tests/test_object_entry.c
    tests/test_filesystem.c
    tests/test_image_creator.c
    tests/test_boot_loader.c
    tests/test_write_comprehensive.c
    tests/test_xat.c
    tests/test_xat_copy.c
    tests/test_parity.c
    tests/test_wildmatch.c
)

add_executable(ndfs_tests ${TEST_SOURCES})
target_link_libraries(ndfs_tests PRIVATE ndfs)
target_include_directories(ndfs_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

if(NOT MSVC)
    target_compile_options(ndfs_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME ndfs_tests COMMAND ndfs_tests)

# ── ndtool CLI ──────────────────────────────────────────────────────

add_executable(ndtool
    tools/ndtool/ndtool.c
    tools/ndtool/cmd_list.c
    tools/ndtool/cmd_extract.c
    tools/ndtool/cmd_put.c
    tools/ndtool/cmd_delete.c
    tools/ndtool/cmd_user.c
    tools/ndtool/cmd_info.c
    tools/ndtool/cmd_create.c
    tools/ndtool/cmd_shell.c
    tools/ndtool/parity.c
)
target_link_libraries(ndtool ndfs)
target_include_directories(ndtool PRIVATE ${CMAKE_SOURCE_DIR}/include tools/ndtool)

if(NOT MSVC)
    target_compile_options(ndtool PRIVATE -Wall -Wextra)
endif()
