cmake_minimum_required(VERSION 3.16)


project(CoSchedSIMD VERSION 1.0.0 LANGUAGES CXX)

# Header-only interface library 
add_library(cossched INTERFACE)
add_library(CoSchedSIMD::cossched ALIAS cossched)
target_include_directories(cossched INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cossched INTERFACE cxx_std_17)

# Auto-detect AVX2 for downstream consumers
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
if(COMPILER_SUPPORTS_AVX2)
  target_compile_options(cossched INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-mavx2>)
endif()

# Examples (opt-in) 
option(CO_SCHED_BUILD_EXAMPLES "Build example programs" OFF)
if(CO_SCHED_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()
