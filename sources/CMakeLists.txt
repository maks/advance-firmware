# SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
#
# Copyright (c) 2026 xiphonics, inc.
#
# Inherited BSD-licensed material remains BSD-3-Clause.
# New original contributions by xiphonics, inc. are GPL-3.0-only.
# Previously granted BSD permissions are preserved. See LICENSING.md.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json")
option(ENABLE_LOGGING "Enable Trace logging" OFF)
option(ENABLE_PROFILING "Enable profiling" OFF)
option(WASM "Build for WebAssembly (browser)" OFF)

if(WASM)
  project(picoTrackerWasm LANGUAGES C CXX)
  include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/wasm/toolchain-check.cmake")
  add_subdirectory(Adapters/wasm)
  return()
endif()

add_compile_options(
  -Werror
  -Wall
  -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
  -fstack-usage
  -fcallgraph-info=su # include above info in callgraph
  -Wstack-usage=2074 # allows high watermark at the time this option was added TODO: reduce this for safety and/or reduce stack size
  
  # braids
  -Wno-unused-variable
  -Wno-array-bounds

  # stm HAL
  -Wno-unused-parameter
  -Wno-missing-field-initializers
)

message("picoTracker Advance Build")
add_definitions(-DADV)
  
# Enable basic control REPL over serial port on non-Release ADV builds.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  remove_definitions(-DSERIAL_REPL)
else()
  add_definitions(-DSERIAL_REPL)
endif()

# Enable profiling
add_compile_definitions(ENABLE_PROFILING=$<BOOL:${ENABLE_PROFILING}>)

# Enable stats for RTOS
#add_definitions(-DRTOS_STATS)

# FreeRTOS goes before compiler definitions for cubemx since it
# does it's own thing (doesn't compile after those definitions)
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM
  INTERFACE
  Adapters/adv/config # The config file directory
  Adapters/adv/platform
)

target_compile_definitions(freertos_config
  INTERFACE
  projCOVERAGE_TEST=0)
set(FREERTOS_PORT GCC_ARM_CM7 CACHE STRING "")

# ------- cmake/gcc-arm-none-eabi.cmake START -------

set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Some default GCC settings
# arm-none-eabi- must be part of path environment
set(TOOLCHAIN_PREFIX                arm-none-eabi-)

set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_LINKER                    ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXEUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")

set(CMAKE_C_FLAGS_DEBUG "-Os -g3")
set(CMAKE_C_FLAGS_RELEASE "-O3 -g0 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-Os -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g0 -DNDEBUG")
option(RELEASE_DEBUG_SYMBOLS "Keep symbols for optimized firmware profiling" OFF)
if(RELEASE_DEBUG_SYMBOLS)
  set(CMAKE_C_FLAGS_RELEASE "-O3 -g -DNDEBUG")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -DNDEBUG")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-use-cxa-atexit")
  
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()
  
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,--start-group -lstdc++ -lsupc++ -Wl,--end-group")
# ------- cmake/gcc-arm-none-eabi.cmake STOP -------

project(picoTrackerAdvance C CXX ASM)
set(CMAKE_CXX_FLAGS
  "${CMAKE_CXX_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/config/no_heap.h")

include_directories(${PROJECT_SOURCE_DIR}/Externals)
add_subdirectory(Externals/FreeRTOS-Kernel)
add_subdirectory(Externals/FatFs)
add_subdirectory(Externals/tinyusb)
  
# TODO: Add ETL here
# cubemx and freertos is included on all targets
link_libraries(stm32cubemx freertos_kernel)

#  set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/config/lv_conf.h" CACHE STRING "Path to LVGL config")
#  set(LV_CONF_BUILD_DISABLE_DEMOS 1)
#  set(LV_CONF_BUILD_DISABLE_EXAMPLES 1)
#  add_subdirectory(Externals/lvgl)

add_subdirectory(Adapters/adv)

# Enable ETL debug mode only for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(ETL_DEBUG)
endif()


add_subdirectory(UIFramework)
add_subdirectory(System)
if(ENABLE_LOGGING)
  target_compile_definitions(system_console PRIVATE ENABLE_LOGGING)
endif()

add_subdirectory(Application)
add_subdirectory(Externals)
add_subdirectory(Services)
add_subdirectory(Foundation)
