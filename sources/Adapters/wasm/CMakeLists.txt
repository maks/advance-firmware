cmake_minimum_required(VERSION 3.13)

include_directories(
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/config"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform")

add_compile_definitions(WASM)

set(PICOTRACKER_WEB_WASM_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../web/public/wasm"
  CACHE PATH "Output directory consumed by the Vite workbench")
file(MAKE_DIRECTORY "${PICOTRACKER_WEB_WASM_DIR}")

add_compile_options(-pthread)

add_library(platform_mutex mutex/WasmMutex.cpp)
target_include_directories(platform_mutex PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_timer timer/WasmTimer.cpp)
target_include_directories(platform_timer PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_timer PUBLIC system_timer foundation)

add_library(platform_filesystem
  filesystem/WasmFile.cpp
  filesystem/WasmFileSystem.cpp
  filesystem/WasmStorageBridge.cpp)
target_include_directories(platform_filesystem PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_snapshot
  platform/WasmApplicationSnapshot.cpp)
target_include_directories(platform_snapshot PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_logging logging/WasmTrace.cpp)
target_include_directories(platform_logging PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_tracing
  tracing/InputFrameLatencyTracker.cpp
  tracing/WasmProfiler.cpp
  tracing/Benchmark.cpp)
target_include_directories(platform_tracing PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform
  platform/platform.cpp
  process/WasmProcess.cpp)
target_include_directories(platform PUBLIC
  "${PROJECT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform")
target_link_libraries(platform PUBLIC platform_mutex)

add_library(platform_recording
  audio/UnavailableRecording.cpp)
target_include_directories(platform_recording PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_audio
  audio/AudioWorklet.cpp
  audio/OutputResampler.cpp
  audio/WasmAudio.cpp
  audio/WasmAudioDriver.cpp)
target_include_directories(platform_audio PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_audio PUBLIC platform_snapshot)

add_library(platform_midi
  midi/NullMidiService.cpp)
target_include_directories(platform_midi PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(platform_system
  system/WasmSamplePool.cpp
  system/WasmSystem.cpp
  system/error_bitmaps.c)
target_include_directories(platform_system PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(platform_system PUBLIC
  platform_audio
  platform_filesystem
  platform_logging
  platform_tracing
  platform_midi
  platform_timer
  platform
  platform_recording)

add_library(platform_display
  gui/WasmEventManager.cpp
  gui/WasmGUIFactory.cpp
  gui/WasmGUIWindowImp.cpp
  display/WasmDisplayState.cpp
  "${PROJECT_SOURCE_DIR}/Adapters/adv/display/pixel_targets.c"
  display/WasmDisplayBackend.cpp
  display/display_backend_wasm.cpp)
target_include_directories(platform_display PUBLIC "${PROJECT_SOURCE_DIR}")
target_compile_options(platform_display PRIVATE --use-port=sdl2 -Wno-deprecated-declarations)
target_link_libraries(platform_display PUBLIC platform_system system_display)

add_subdirectory("${PROJECT_SOURCE_DIR}/UIFramework"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/UIFramework")
add_subdirectory("${PROJECT_SOURCE_DIR}/System"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/System")
target_compile_options(system_console PRIVATE -UNDEBUG)
add_subdirectory("${PROJECT_SOURCE_DIR}/Foundation"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Foundation")
add_subdirectory("${PROJECT_SOURCE_DIR}/Externals"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Externals")
target_compile_options(samplerate PRIVATE -include stddef.h)
add_subdirectory("${PROJECT_SOURCE_DIR}/Services"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Services")
add_subdirectory("${PROJECT_SOURCE_DIR}/Application"
                 "${CMAKE_CURRENT_BINARY_DIR}/core/Application")
target_compile_options(application_instruments PRIVATE -Werror=non-pod-varargs)

execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
  OUTPUT_VARIABLE PICOTRACKER_WASM_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT PICOTRACKER_WASM_COMMIT)
  set(PICOTRACKER_WASM_COMMIT "unknown")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env GIT_OPTIONAL_LOCKS=0
          git diff-index --quiet HEAD --
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
  RESULT_VARIABLE PICOTRACKER_WASM_DIRTY_RESULT
  ERROR_QUIET)
if(PICOTRACKER_WASM_DIRTY_RESULT EQUAL 0)
  set(PICOTRACKER_WASM_DIRTY false)
else()
  set(PICOTRACKER_WASM_DIRTY true)
endif()

string(TIMESTAMP PICOTRACKER_WASM_BUILT_AT "%Y-%m-%dT%H:%M:%SZ" UTC)

add_executable(picotracker_wasm
  input/InputMap.cpp
  input/InputRepeatTracker.cpp
  main/main.cpp
  platform/wasm_bridge.cpp)

set(PICOTRACKER_WEB_FACTORY_CONTENT_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../data/web-factory-content")
file(GLOB_RECURSE PICOTRACKER_WEB_FACTORY_CONTENT_FILES CONFIGURE_DEPENDS
  "${PICOTRACKER_WEB_FACTORY_CONTENT_DIR}/*")
set_property(TARGET picotracker_wasm APPEND PROPERTY LINK_DEPENDS
  ${PICOTRACKER_WEB_FACTORY_CONTENT_FILES})

set_source_files_properties(main/main.cpp PROPERTIES
  COMPILE_OPTIONS
    "-Wno-error;-Wno-unused-parameter;-Wno-deprecated-declarations")

target_include_directories(picotracker_wasm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(picotracker_wasm PRIVATE
  platform_system
  platform_display
  -Wl,--whole-archive
  application
  -Wl,--no-whole-archive
  services_audio
  services_midi
  platform_filesystem
  platform_midi
  platform_timer
  platform
  platform_recording
  foundation
  system_display)

target_compile_definitions(picotracker_wasm PRIVATE
  PICOTRACKER_WASM_BUILD_COMMIT="${PICOTRACKER_WASM_COMMIT}"
  PICOTRACKER_WASM_BUILD_DIRTY=${PICOTRACKER_WASM_DIRTY}
  PICOTRACKER_WASM_BUILD_TIME="${PICOTRACKER_WASM_BUILT_AT}")
target_compile_options(picotracker_wasm PRIVATE
  -pthread
  --use-port=sdl2
  -Wall
  -Wextra
  -Werror)
target_link_options(picotracker_wasm PRIVATE
  -pthread
  -lidbfs.js
  "SHELL:--embed-file \"${PICOTRACKER_WEB_FACTORY_CONTENT_DIR}@/defaults/factory\""
  --use-port=sdl2
  "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/picotracker-core-link.map"
  -sERROR_ON_UNDEFINED_SYMBOLS=1
  -sPROXY_TO_PTHREAD=1
  -sOFFSCREENCANVAS_SUPPORT=1
  "-sOFFSCREENCANVASES_TO_PTHREAD=#picotracker-canvas"
  -sPTHREAD_POOL_SIZE=4
  -sWASM_WORKERS=1
  -sAUDIO_WORKLET=1
  $<$<CONFIG:Debug>:-sWEBAUDIO_DEBUG=1>
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createPicoTrackerModule
  -sINITIAL_MEMORY=268435456
  -sENVIRONMENT=web,worker,worklet
  "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_PicoTracker_Wasm_GetBuildMetadataJson','_PicoTracker_Wasm_GetState','_PicoTracker_Wasm_BootstrapAudio','_PicoTracker_Wasm_MarkAudioUnavailable','_PicoTracker_Wasm_ConfigureAudio','_PicoTracker_Wasm_RequestShutdown','_PicoTracker_Wasm_GetLastError','_PicoTracker_Wasm_DiagnosticFail','_PicoTracker_Wasm_SetAction','_PicoTracker_Wasm_RepeatAction','_PicoTracker_Wasm_ReleaseAllActions','_PicoTracker_Wasm_GetActionMask','_PicoTracker_Wasm_GetActionGeneration','_PicoTracker_Wasm_GetLastAction','_PicoTracker_Wasm_UnlockAudio','_PicoTracker_Wasm_StopAudio','_PicoTracker_Wasm_GetAudioState','_PicoTracker_Wasm_GetAudioError','_PicoTracker_Wasm_GetAudioMetrics','_PicoTracker_Wasm_GetAudioMetricsSnapshot','_PicoTracker_Wasm_GetAudioErrorSnapshot','_PicoTracker_Wasm_GetBrowserSnapshots','_PicoTracker_Wasm_CaptureFrameRgba']"
  "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','HEAPU8','HEAPU32','PThread','FS','IDBFS','addRunDependency','removeRunDependency']")

set_target_properties(picotracker_wasm PROPERTIES
  OUTPUT_NAME picotracker
  SUFFIX ".js"
  RUNTIME_OUTPUT_DIRECTORY "${PICOTRACKER_WEB_WASM_DIR}")

add_custom_command(TARGET picotracker_wasm POST_BUILD
  COMMAND "${CMAKE_COMMAND}"
    "-DWASM_BINARY=${PICOTRACKER_WEB_WASM_DIR}/picotracker.wasm"
    "-DLINK_MAP=${CMAKE_CURRENT_BINARY_DIR}/picotracker-core-link.map"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/VerifyCoreSymbols.cmake"
  COMMENT "Verifying native Advance core link closure"
  VERBATIM)
