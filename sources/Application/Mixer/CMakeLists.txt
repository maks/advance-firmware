# SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
#
# Copyright (c) 2026 xiphonics, inc.
#
# Inherited BSD-licensed material remains BSD-3-Clause.
# New original contributions by xiphonics, inc. are GPL-3.0-only.
# Previously granted BSD permissions are preserved. See LICENSING.md.

add_library(application_mixer
  LineInMixer.cpp
  MixBus.cpp
  MixerService.cpp
)

target_link_libraries(application_mixer PUBLIC
  application_audio
  application_effects
  application_model
  system_console
  platform
)

target_include_directories(application_mixer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
