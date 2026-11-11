add_library(platform_audio
  advAudio.h advAudio.cpp
  advAudioDriver.h advAudioDriver.cpp
  tlv320aic3204.c
  record.cpp
  advLineInCapture.cpp
)

target_link_libraries(platform_audio PUBLIC
  fatfs
  platform_usb
)

target_include_directories(platform_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR})
