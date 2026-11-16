# SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
#
# Copyright (c) 2026 xiphonics, inc.
#
# Inherited BSD-licensed material remains BSD-3-Clause.
# New original contributions by xiphonics, inc. are GPL-3.0-only.
# Previously granted BSD permissions are preserved. See LICENSING.md.

add_library(application_views_modaldialogs
  MessageBox.cpp
  FullScreenBox.cpp
  TextInputModalView.cpp
  KeyboardModalView.cpp
  RenderProgressModal.cpp
)

target_link_libraries(application_views_modaldialogs PUBLIC
  application_views_baseclasses
  application_player
  system_filesystem
)

target_include_directories(application_views_modaldialogs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
