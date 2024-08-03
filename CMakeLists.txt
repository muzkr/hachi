cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

#include(pico_extras_import_optional.cmake)

project(hachi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(PICO_BOARD "buildhat")
set(PICO_BOARD_HEADER_DIRS "${CMAKE_SOURCE_DIR}/board")

set(PICO_DEFAULT_BOOT_STAGE2_FILE ${CMAKE_SOURCE_DIR}/boot2/boot2_hachi.S)

# set(PICO_NO_FPGA_CHECK 1)

# Initialize the SDK
pico_sdk_init()

add_subdirectory(src hachi)
