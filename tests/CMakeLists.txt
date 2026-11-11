# SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
#
# Copyright (c) 2026 xiphonics, inc.
#
# Inherited BSD-licensed material remains BSD-3-Clause.
# New original contributions by xiphonics, inc. are GPL-3.0-only.
# Previously granted BSD permissions are preserved. See LICENSING.md.

cmake_minimum_required(VERSION 3.13)

project(picoTrackerHostTests C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BRAIDS_SOURCES
  ../sources/Externals/braids/analog_oscillator.cc
  ../sources/Externals/braids/digital_oscillator.cc
  ../sources/Externals/braids/macro_oscillator.cc
  ../sources/Externals/braids/quantizer.cc
  ../sources/Externals/braids/random.cc
  ../sources/Externals/braids/resources.cc
  ../sources/Externals/braids/settings.cc
)

add_executable(picoTracker_tests
  text_highlight_tests.cpp
  remote_ui_protocol_tests.cpp
  ../sources/System/RemoteUI/RemoteUIProtocol.cpp
  adv_usb_host_power_policy_tests.cpp
  audio_voice_tests.cpp
  audio_output_silence_tests.cpp
  usb_audio_handoff_tests.cpp
  ../sources/Adapters/adv/usb/usb_audio_handoff.cpp
  ../sources/Services/Audio/AudioDriver.cpp
  line_in_mixer_tests.cpp
  ../sources/Application/Mixer/LineInMixer.cpp
  delay_output_effect_tests.cpp
  dattorro_reverb_tests.cpp
  midi_in_device_tests.cpp
  midi_instrument_tests.cpp
  sample_source_instrument_tests.cpp
  decent_sampler_preset_tests.cpp
  msampler_instrument_tests.cpp
  sid_instrument_tests.cpp
  pico_swarm_instrument_tests.cpp
  dwt_profiler_tests.cpp
  fm6_instrument_tests.cpp
  fm6_dx7_sysex_tests.cpp
  instrument_audition_tests.cpp
  starloom_instrument_tests.cpp
  insert_effect_slot_tests.cpp
  model_allocation_tests.cpp
  project_channel_count_tests.cpp
  project_output_effect_tests.cpp
  stubs/application_link_stubs.cpp
  stubs/nanoprintf_impl.c
  browser_entry_sort_tests.cpp
  browser_paging_tests.cpp
  browser_path_tests.cpp
  big_hex_field_tests.cpp
  filesystem_api_contract_tests.cpp
  filesystem_path_tests.cpp
  foundation_types_tests.cpp
  charger_host_power_tests.cpp
  charger_watchdog_tests.cpp
  i2c_recovery_tests.cpp
  command_list_tests.cpp
  command_help_state_tests.cpp
  instrument_pool_tests.cpp
  output_effect_slot_tests.cpp
  play_button_behavior_tests.cpp
  pitch_offset_field_tests.cpp
  player_channel_wet_dry_tests.cpp
  voice_filter_graph_tests.cpp
  voice_envelope_graph_tests.cpp
  voice_screen_utils_tests.cpp
  sample_slicer_tests.cpp
  row_fx_tests.cpp
  sample_pool_path_tests.cpp
  sample_storage_tests.cpp
  selection_button_behavior_tests.cpp
  song_loop_range_tests.cpp
  import_while_playing_tests.cpp
  test_main.cpp
  wavheader_tests.cpp
  display_renderer_tests.cpp
  wasm_input_tests.cpp
  wasm_audio_tests.cpp
  wasm_storage_tests.cpp
  ../sources/System/Display/DisplayRenderer.c
  ../sources/System/Display/font.c
  ../sources/Adapters/wasm/input/InputMap.cpp
  ../sources/Adapters/wasm/input/InputRepeatTracker.cpp
  ../sources/Adapters/wasm/audio/OutputResampler.cpp
  ../sources/Adapters/wasm/audio/AudioWorklet.cpp
  ../sources/Adapters/wasm/audio/WasmAudioDriver.cpp
  ../sources/Adapters/wasm/filesystem/WasmFileSystem.cpp
  ../sources/Adapters/wasm/filesystem/WasmFile.cpp
  ../sources/Adapters/wasm/filesystem/WasmStorageBridge.cpp
  ../sources/Adapters/wasm/tracing/InputFrameLatencyTracker.cpp
  ../sources/Adapters/wasm/tracing/WasmProfiler.cpp
  ../sources/Services/Audio/AudioDriver.cpp
  stubs/System/FileSystem/FakeFileSystem.cpp
  ../sources/Application/Instruments/CommandList.cpp
  ../sources/Application/Instruments/DecentSamplerPreset.cpp
  ../sources/Application/Instruments/Filters.cpp
  ../sources/Application/Instruments/InstrumentBank.cpp
  ../sources/Application/Instruments/InstrumentNameVariable.cpp
  ../sources/Application/Instruments/MidiInstrument.cpp
  ../sources/Application/Instruments/MSamplerInstrument.cpp
  ../sources/Application/Instruments/SampleStreamService.cpp
  ../sources/Application/Instruments/NoneInstrument.cpp
  ../sources/Application/Instruments/SRPUpdaters.cpp
  ../sources/Application/Instruments/SampleInstrument.cpp
  ../sources/Application/Instruments/SampleSourceInstrument.cpp
  ../sources/Application/Instruments/SamplePool.cpp
  ../sources/Application/Instruments/SampleVariable.cpp
  stubs/TestStreamingSamplePool.cpp
  ../sources/Application/Instruments/SIDInstrument.cpp
  ../sources/Application/Instruments/SoundSource.cpp
  ../sources/Application/Instruments/picoSwarmInstrument.cpp
  ../sources/Application/Instruments/WavFile.cpp
  ../sources/Application/Instruments/WavFileWriter.cpp
  ../sources/Application/Instruments/AdvanceFM6/FM6Dx7Patch.cpp
  ../sources/Application/Effects/DelayOutputEffect.cpp
  ../sources/Application/Effects/DattorroReverb.cpp
  ../sources/Application/Effects/OutputEffect.cpp
  ../sources/Application/Effects/OutputEffectSlot.cpp
  ../sources/Application/Effects/DattorroReverbOutputEffect.cpp
  ../sources/Application/Model/Chain.cpp
  ../sources/Application/Model/Config.cpp
  ../sources/Application/Model/Groove.cpp
  ../sources/Application/Model/Phrase.cpp
  ../sources/Application/Model/Project.cpp
  ../sources/Application/Model/Scale.cpp
  ../sources/Application/Model/Song.cpp
  ../sources/Application/Model/Table.cpp
  ../sources/Application/Persistency/PersistencyDocument.cpp
  ../sources/Application/Utils/HexBuffers.cpp
  ../sources/Application/Utils/char.cpp
  ../sources/Application/Instruments/LFOSettings.cpp
  ../sources/Application/Instruments/FM6Instrument.cpp
  ../sources/Application/Instruments/StarLoomInstrument.cpp
  ../sources/Application/Instruments/AdvanceFM6/FM6Voice.cpp
  ../sources/Application/Instruments/VoiceParameters.cpp
  ../sources/Application/Player/AudioVoice.cpp
  ../sources/Application/Player/InsertEffectSlot.cpp
  ../sources/Application/Player/LFOProcessor.cpp
  ../sources/Application/Player/RowFx.cpp
  ../sources/Application/Player/SyncMaster.cpp
  ../sources/Application/Player/VoiceADSR.cpp
  ../sources/Services/Midi/MidiInDevice.cpp
  ../sources/Services/Midi/MidiNoteTracker.cpp
  ../sources/Application/Views/VoiceFilterGraph.cpp
  ../sources/Application/Views/VoiceEnvelopeGraph.cpp
  ../sources/Foundation/Observable.cpp
  ../sources/Foundation/Services/Service.cpp
  ../sources/Foundation/Services/ServiceRegistry.cpp
  ../sources/Foundation/Variables/Variable.cpp
  ../sources/Foundation/Variables/VariableContainer.cpp
  ../sources/Foundation/Variables/WatchedVariable.cpp
  ../sources/Application/Instruments/WavHeader.cpp
  ../sources/Externals/SRC/samplerate.c
  ../sources/Externals/SRC/src_linear.c
  ../sources/Externals/SRC/src_sinc_stub.c
  ../sources/Application/Instruments/AdvanceSID/AdvanceSID.cpp
  ../sources/Externals/TinyXML2/tinyxml2_printer_only.cpp
  ../sources/Externals/TinyXML2/tinyxml2adapter.cpp
  ../sources/Externals/yxml/yxml.c
  ../sources/System/io/Status.cpp
  ${BRAIDS_SOURCES}
)

target_include_directories(picoTracker_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Application/Model
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Application/Persistency
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Foundation
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Foundation/Variables
)

target_compile_definitions(picoTracker_tests PRIVATE
  HOST_TEST
  HAVE_STDBOOL_H
  PACKAGE="picoTrackerHostTests"
  VERSION="test"
)

add_executable(picoTracker_launchpad_tests
  test_main.cpp
  launchpad_usb_passthrough_tests.cpp
  stubs/launchpad_control_surface_link_stubs.cpp
  ../sources/Services/Midi/MidiInDevice.cpp
  ../sources/Services/Midi/MidiNoteTracker.cpp
  ../sources/Services/Midi/ControlSurface/ControlSurfaceDevice.cpp
  ../sources/Services/Midi/ControlSurface/LaunchpadMiniMk3Surface.cpp
  ../sources/Foundation/Observable.cpp
)

target_include_directories(picoTracker_launchpad_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Services/Midi
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Application/Model
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Application/Persistency
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Foundation
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Foundation/Variables
)

target_compile_definitions(picoTracker_launchpad_tests PRIVATE
  HOST_TEST
  HAVE_STDBOOL_H
  PACKAGE="picoTrackerHostTests"
  VERSION="test"
)

add_executable(picoTracker_boundary_tests
  test_main.cpp
  instrument_import_path_tests.cpp
  persistency_project_path_tests.cpp
  select_project_view_tests.cpp
  theme_import_path_tests.cpp
  boundary_stubs/System/Console/nanoprintf.cpp
  stubs/System/FileSystem/FakeFileSystem.cpp
  ../sources/Application/Instruments/WavHeader.cpp
  ../sources/Application/Effects/OutputEffect.cpp
  ../sources/Application/Effects/OutputEffectSlot.cpp
  ../sources/Application/Model/Config.cpp
  ../sources/Application/Persistency/PersistencyDocument.cpp
  ../sources/Application/Persistency/PersistencyService.cpp
  ../sources/Application/Persistency/Persistent.cpp
  ../sources/Externals/SRC/samplerate.c
  ../sources/Externals/SRC/src_linear.c
  ../sources/Externals/SRC/src_sinc_stub.c
  ../sources/Externals/TinyXML2/tinyxml2_printer_only.cpp
  ../sources/Externals/TinyXML2/tinyxml2adapter.cpp
  ../sources/Externals/yxml/yxml.c
  ../sources/Foundation/Observable.cpp
  ../sources/Foundation/Services/Service.cpp
  ../sources/Foundation/Services/ServiceRegistry.cpp
  ../sources/Foundation/Services/SubService.cpp
  ../sources/Foundation/Variables/Variable.cpp
  ../sources/Foundation/Variables/VariableContainer.cpp
  ../sources/Foundation/Variables/WatchedVariable.cpp
  ../sources/System/io/Status.cpp
)

target_include_directories(picoTracker_boundary_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/boundary_stubs
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Application/Model
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Application/Persistency
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Foundation
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Foundation/Services
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Foundation/Variables
)

target_compile_definitions(picoTracker_boundary_tests PRIVATE
  HOST_TEST
  HAVE_STDBOOL_H
  PACKAGE="picoTrackerHostTests"
  VERSION="test"
)

# End-to-end instrument import test: real PersistencyService + real Persistent +
# real SampleSourceInstrument together (issue #370).
add_executable(picoTracker_instr_import_e2e_tests
  test_main.cpp
  instrument_import_e2e_tests.cpp
  stubs/application_link_stubs.cpp
  stubs/nanoprintf_impl.c
  stubs/System/FileSystem/FakeFileSystem.cpp
  ../sources/Application/Instruments/CommandList.cpp
  ../sources/Application/Instruments/DecentSamplerPreset.cpp
  ../sources/Application/Instruments/MSamplerInstrument.cpp
  ../sources/Application/Instruments/SampleStreamService.cpp
  ../sources/Application/Instruments/Filters.cpp
  ../sources/Application/Instruments/I_Instrument.cpp
  ../sources/Application/Instruments/InstrumentBank.cpp
  ../sources/Application/Instruments/InstrumentNameVariable.cpp
  ../sources/Application/Instruments/MidiInstrument.cpp
  ../sources/Application/Instruments/NoneInstrument.cpp
  ../sources/Application/Instruments/SRPUpdaters.cpp
  ../sources/Application/Instruments/SampleInstrument.cpp
  ../sources/Application/Instruments/SampleSourceInstrument.cpp
  ../sources/Application/Instruments/SamplePool.cpp
  ../sources/Application/Instruments/SampleVariable.cpp
  ../sources/Application/Instruments/SIDInstrument.cpp
  ../sources/Application/Instruments/SoundSource.cpp
  ../sources/Application/Instruments/picoSwarmInstrument.cpp
  ../sources/Application/Instruments/WavFile.cpp
  ../sources/Application/Instruments/WavFileWriter.cpp
  ../sources/Application/Instruments/LFOSettings.cpp
  ../sources/Application/Instruments/FM6Instrument.cpp
  ../sources/Application/Instruments/StarLoomInstrument.cpp
  ../sources/Application/Instruments/VoiceParameters.cpp
  ../sources/Application/Instruments/AdvanceFM6/FM6Dx7Patch.cpp
  ../sources/Application/Instruments/AdvanceFM6/FM6Voice.cpp
  ../sources/Application/Instruments/AdvanceSID/AdvanceSID.cpp
  ../sources/Application/Effects/DattorroReverb.cpp
  ../sources/Application/Effects/DelayOutputEffect.cpp
  ../sources/Application/Effects/OutputEffect.cpp
  ../sources/Application/Effects/OutputEffectSlot.cpp
  ../sources/Application/Effects/DattorroReverbOutputEffect.cpp
  ../sources/Application/Model/Chain.cpp
  ../sources/Application/Model/Config.cpp
  ../sources/Application/Model/Groove.cpp
  ../sources/Application/Model/Phrase.cpp
  ../sources/Application/Model/Project.cpp
  ../sources/Application/Model/Scale.cpp
  ../sources/Application/Model/Song.cpp
  ../sources/Application/Model/Table.cpp
  ../sources/Application/Persistency/PersistencyDocument.cpp
  ../sources/Application/Persistency/PersistencyService.cpp
  ../sources/Application/Persistency/Persistent.cpp
  ../sources/Application/Utils/HexBuffers.cpp
  ../sources/Application/Utils/char.cpp
  ../sources/Application/Player/AudioVoice.cpp
  ../sources/Application/Player/InsertEffectSlot.cpp
  ../sources/Application/Player/LFOProcessor.cpp
  ../sources/Application/Player/RowFx.cpp
  ../sources/Application/Player/SyncMaster.cpp
  ../sources/Application/Player/VoiceADSR.cpp
  ../sources/Application/Views/VoiceFilterGraph.cpp
  ../sources/Application/Views/VoiceEnvelopeGraph.cpp
  ../sources/Foundation/Observable.cpp
  ../sources/Foundation/Services/Service.cpp
  ../sources/Foundation/Services/ServiceRegistry.cpp
  ../sources/Foundation/Services/SubService.cpp
  ../sources/Foundation/Variables/Variable.cpp
  ../sources/Foundation/Variables/VariableContainer.cpp
  ../sources/Foundation/Variables/WatchedVariable.cpp
  ../sources/Application/Instruments/WavHeader.cpp
  ../sources/Externals/SRC/samplerate.c
  ../sources/Externals/SRC/src_linear.c
  ../sources/Externals/SRC/src_sinc_stub.c
  ../sources/Externals/TinyXML2/tinyxml2_printer_only.cpp
  ../sources/Externals/TinyXML2/tinyxml2adapter.cpp
  ../sources/Externals/yxml/yxml.c
  ../sources/System/io/Status.cpp
  ${BRAIDS_SOURCES}
)
target_include_directories(picoTracker_instr_import_e2e_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stubs
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Externals/SRC/include
  ${CMAKE_CURRENT_LIST_DIR}/../sources
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Application/Model
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Application/Persistency
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Foundation
  ${CMAKE_CURRENT_LIST_DIR}/../sources/Foundation/Variables
)
target_compile_definitions(picoTracker_instr_import_e2e_tests PRIVATE
  HOST_TEST
  HAVE_STDBOOL_H
  PACKAGE="picoTrackerHostTests"
  VERSION="test"
)

add_library(header_display_backend OBJECT ../sources/Adapters/adv/display/text_fonts.c ../sources/Adapters/adv/display/display.c ../sources/Adapters/adv/display/pixel_targets.c)
target_include_directories(header_display_backend PRIVATE display_stubs ../sources)
add_executable(picoTracker_header_font_tests
  test_main.cpp
  header_font_tests.cpp
  ../sources/Application/Views/BaseClasses/UIField.cpp
  $<TARGET_OBJECTS:header_display_backend>
)
target_include_directories(picoTracker_header_font_tests PRIVATE
  display_stubs ../sources ../sources/Externals
)
target_compile_definitions(picoTracker_header_font_tests PRIVATE HOST_TEST)
target_include_directories(picoTracker_tests BEFORE PRIVATE display_stubs)
