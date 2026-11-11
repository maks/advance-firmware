add_library(system_display
  DisplayRenderer.c
  font.c
)

target_include_directories(system_display PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${PROJECT_SOURCE_DIR}
)
