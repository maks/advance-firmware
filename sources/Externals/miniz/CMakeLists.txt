add_library(miniz STATIC miniz.c)
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(miniz PRIVATE
  MINIZ_NO_ARCHIVE_WRITING
  MINIZ_NO_STDIO
  MINIZ_NO_ZLIB_APIS
  MINIZ_NO_DEFLATE_APIS
)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  # ZIP reader/decompressor frames are intentionally run on the dedicated
  # extraction task, whose stack is sized for miniz's documented working set.
  target_compile_options(miniz PRIVATE -Wno-stack-usage)
endif()
