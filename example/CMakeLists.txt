# Each .cpp in this directory produces a standalone binary.

file(GLOB examples RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cpp)

foreach(src ${examples})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE CoSchedSIMD::cossched)
  target_compile_options(${name} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
  )
endforeach()
