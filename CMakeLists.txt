cmake_minimum_required(VERSION 3.14)
project(nets C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED NETS_BUILD_VERSION)
    set(NETS_BUILD_VERSION 0)
endif()


option(NETS_NATIVE "Enable native hardware optimizations" OFF)
option(NETS_BUILD_TESTS "Build nets tests" OFF)

set(NETS_TEST_EXTERNAL_ARTIFACTS OFF)
if(NETS_BUILD_TESTS AND NETS_TEST_SHARED_LIBRARY)
    set(NETS_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(NETS_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libnets.c PROPERTIES
    COMPILE_DEFINITIONS KC_NETS_BUILD_VERSION=${NETS_BUILD_VERSION}
)

if(NOT NETS_TEST_EXTERNAL_ARTIFACTS)
    # Main executable
    add_executable(nets
        src/nets.c
        src/libnets.c
    )

    target_include_directories(nets PRIVATE src)

    if(UNIX AND NOT ANDROID)
        target_link_libraries(nets pthread m)
    endif()

    if(WIN32)
        target_link_libraries(nets ws2_32)
    endif()

    # Static library
    add_library(nets_static STATIC src/libnets.c)

    set_target_properties(nets_static PROPERTIES OUTPUT_NAME "nets")

    target_include_directories(nets_static PRIVATE src)

    # Shared library
    add_library(nets_shared SHARED src/libnets.c)

    set_target_properties(nets_shared PROPERTIES OUTPUT_NAME "nets")
    set_target_properties(nets_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

    target_include_directories(nets_shared PRIVATE src)

    if(UNIX AND NOT ANDROID)
        target_link_libraries(nets_shared pthread m)
    endif()

    if(WIN32)
        target_link_libraries(nets_shared ws2_32)
    endif()
endif()

if(NETS_BUILD_TESTS)
    enable_testing()

    if(NETS_TEST_SHARED_LIBRARY)
        add_library(nets_test_shared SHARED IMPORTED)
        set_target_properties(nets_test_shared PROPERTIES
            IMPORTED_LOCATION "${NETS_TEST_SHARED_LIBRARY}"
        )
        if(NETS_TEST_IMPORT_LIBRARY)
            set_target_properties(nets_test_shared PROPERTIES
                IMPORTED_IMPLIB "${NETS_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(NETS_TEST_SHARED_TARGET nets_shared)
    endif()

    if(TARGET nets_test_shared)
        set(NETS_TEST_SHARED_TARGET nets_test_shared)
    endif()

    add_executable(nets_contract_test src/test.c)
    target_include_directories(nets_contract_test PRIVATE src)
    target_link_libraries(nets_contract_test PRIVATE ${NETS_TEST_SHARED_TARGET})

    if(UNIX AND NOT ANDROID)
        target_link_libraries(nets_contract_test PRIVATE pthread m)
    endif()
    if(WIN32)
        target_link_libraries(nets_contract_test PRIVATE ws2_32)
    endif()

    add_custom_command(TARGET nets_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${NETS_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:nets_contract_test>
    )

    add_test(NAME library-options-status COMMAND nets_contract_test options-status)
    add_test(NAME library-lifecycle COMMAND nets_contract_test lifecycle)
    add_test(NAME library-signal COMMAND nets_contract_test signal)
    add_test(NAME library-send-guards COMMAND nets_contract_test send-guards)
    add_test(NAME library-tcp-send COMMAND nets_contract_test tcp-send)
    add_test(NAME library-udp-send COMMAND nets_contract_test udp-send)
    add_test(NAME library-multictx-stop COMMAND nets_contract_test multictx-stop)

    if(NETS_TEST_CLI)
        add_executable(nets_test_cli IMPORTED)
        set_target_properties(nets_test_cli PROPERTIES
            IMPORTED_LOCATION "${NETS_TEST_CLI}"
        )
        set(NETS_TEST_CLI_COMMAND nets_test_cli)
    else()
        set(NETS_TEST_CLI_COMMAND nets)
    endif()

    add_test(NAME cli-help COMMAND ${NETS_TEST_CLI_COMMAND} --help)
    add_test(NAME cli-version COMMAND ${NETS_TEST_CLI_COMMAND} --version)
    add_test(NAME cli-unknown COMMAND ${NETS_TEST_CLI_COMMAND} --unknown)
    set_tests_properties(cli-unknown PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli-missing-address COMMAND ${NETS_TEST_CLI_COMMAND})
    set_tests_properties(cli-missing-address PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli-invalid-address COMMAND ${NETS_TEST_CLI_COMMAND} 127.0.0.1:bad)
    set_tests_properties(cli-invalid-address PROPERTIES WILL_FAIL TRUE)
endif()
