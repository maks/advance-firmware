add_library(platform_display
  display.c
  text_fonts.c
  pixel_targets.c
)

target_include_directories(platform_display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
