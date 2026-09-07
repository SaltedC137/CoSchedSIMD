# test/CMakeLists.txt - test suite (enable with -DCO_SCHED_BUILD_TESTS=ON)
# ASan+UBSan are attempted by default; they degrade to a plain build with a
# warning when the runtimes are unavailable. The SleepManager test binary is
# compiled twice (AVX2 and scalar) to verify both tick() paths agree.

include (CheckCXXCompilerFlag)
include (CheckCXXSourceCompiles)

set (CCSS_TEST_SRCS
     test_ringqueue
     test_sleepmanager
     test_channel
     test_scheduler
     test_macros
     test_stress)

option (CCSS_TEST_SANITIZE "Build tests with ASan+UBSan" ON)

set (CCSS_SANITIZERS)
if (CCSS_TEST_SANITIZE)
  set (CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
  check_cxx_source_compiles ("int main () { return 0; }"
                             CCSS_SANITIZERS_AVAILABLE)
  unset (CMAKE_REQUIRED_FLAGS)
  if (CCSS_SANITIZERS_AVAILABLE)
    set (CCSS_SANITIZERS -fsanitize=address,undefined -fno-omit-frame-pointer)
  else ()
    message (WARNING "ASan/UBSan runtime unavailable; tests without sanitizers")
  endif ()
endif ()

check_cxx_compiler_flag ("-mavx2" CCSS_HAS_AVX2)

set (CCSS_BASE_FLAGS -Wall -Wextra)
if (CCSS_HAS_AVX2)
  list (APPEND CCSS_BASE_FLAGS -mavx2)
endif ()

function (ccss_add_test target src)
  add_executable (${target} ${src})
  target_link_libraries (${target} PRIVATE cossched)
  target_compile_options (${target} PRIVATE ${CCSS_BASE_FLAGS})
  if (CCSS_SANITIZERS)
    target_compile_options (${target} PRIVATE ${CCSS_SANITIZERS})
    target_link_options (${target} PRIVATE ${CCSS_SANITIZERS})
  endif ()
  add_test (NAME ${target} COMMAND ${target})
  set_tests_properties (${target} PROPERTIES TIMEOUT 30) # guard against hangs
endfunction ()

foreach (t IN LISTS CCSS_TEST_SRCS)
  ccss_add_test (ccss_test_${t} ${t}.cpp)
endforeach ()

# Re-run the same SleepManager assertions on the non-AVX2 (scalar) path to
# guarantee SIMD/scalar behavioral parity.
ccss_add_test (ccss_test_sleepmanager_scalar test_sleepmanager.cpp)
target_compile_options (ccss_test_sleepmanager_scalar PRIVATE -U__AVX2__)