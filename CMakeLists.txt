cmake_minimum_required(VERSION 3.14)
project(nets C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED NETS_BUILD_VERSION)
    set(NETS_BUILD_VERSION 0)
endif()


option(NETS_NATIVE "Enable native hardware optimizations" OFF)

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

target_include_directories(nets_shared PRIVATE src)

if(UNIX AND NOT ANDROID)
    target_link_libraries(nets_shared pthread m)
endif()

if(WIN32)
    target_link_libraries(nets_shared ws2_32)
endif()
