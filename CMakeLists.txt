cmake_minimum_required( VERSION 3.0.0 )
project(wasm-doom)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

include_directories(
    gifenc
    opl
    sdl_mixer
    src
    src/doom
    )
file (GLOB_RECURSE SRC_FILES *.c)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Oz -Wall \
    -Wno-\#warnings \
    -Wno-macro-redefined \
    -Wno-switch \
    -s WASM=1 \
    -s USE_SDL=2 \
    -s USE_LIBPNG=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s NO_EXIT_RUNTIME=1 \
    -s EXTRA_EXPORTED_RUNTIME_METHODS=['FS'] \
    --no-heap-copy")

if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_executable(index ${SRC_FILES})
  target_link_libraries(index)
  set_target_properties( index PROPERTIES SUFFIX ".html" )

  set(CMAKE_C_FLAGS_DEBUG "-s ASSERTIONS=2 --source-map-base http://localhost:8000/ -g4 --preload-file ${CMAKE_CURRENT_LIST_DIR}/../doom1.wad@doom1.wad")

  add_custom_command(TARGET index POST_BUILD
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMAND sed -i index.wasm.map 's|${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/||g' index.wasm.map
    COMMENT "Fix WebAssembly source map root"
  )

  em_link_js_library(index ${libraryJsFiles})
else()
  add_executable(doom ${SRC_FILES})
  target_link_libraries(doom)
  set_target_properties( doom PROPERTIES SUFFIX ".js" )

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s MODULARIZE=1 -s ASSERTIONS=0")

  em_link_js_library(doom ${libraryJsFiles})
endif()
