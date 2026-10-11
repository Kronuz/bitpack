cmake_minimum_required(VERSION 3.14)
project(bitpack LANGUAGES CXX)

# Header-only: bitpack.hh is the whole implementation.
add_library(bitpack INTERFACE)
target_include_directories(bitpack INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(bitpack INTERFACE cxx_std_20)
add_library(bitpack::bitpack ALIAS bitpack)

# Tests and benchmarks build only when this repo is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(bitpack_test test/test.cc)
  target_link_libraries(bitpack_test PRIVATE bitpack)
  add_test(NAME bitpack COMMAND bitpack_test)
  add_executable(bitpack_portable_test test/test.cc)
  target_link_libraries(bitpack_portable_test PRIVATE bitpack)
  target_compile_definitions(bitpack_portable_test PRIVATE BITPACK_NO_SIMD)
  add_test(NAME bitpack_portable COMMAND bitpack_portable_test)

  option(BITPACK_BENCH_REFERENCE "Fetch FastPFor's SIMD-BP128 as a benchmark-only reference" OFF)
  add_executable(bitpack_bench bench/bench.cc)
  target_link_libraries(bitpack_bench PRIVATE bitpack)
  if(BITPACK_BENCH_REFERENCE)
    # Benchmark-only (Apache-2.0): never part of the bitpack target.
    include(FetchContent)
    FetchContent_Declare(simdcomp_ref
      GIT_REPOSITORY https://github.com/lemire/SIMDCompressionAndIntersection.git
      GIT_TAG        master)
    if(POLICY CMP0169)
      cmake_policy(SET CMP0169 OLD)  # populate only; the reference is not a CMake subproject here
    endif()
    FetchContent_Populate(simdcomp_ref)
    add_library(simdcomp_ref STATIC
      ${simdcomp_ref_SOURCE_DIR}/src/simdbitpacking.cpp
      ${simdcomp_ref_SOURCE_DIR}/src/simdintegratedbitpacking.cpp
      ${simdcomp_ref_SOURCE_DIR}/src/usimdbitpacking.cpp)
    target_include_directories(simdcomp_ref PUBLIC ${simdcomp_ref_SOURCE_DIR}/include)
    target_compile_options(simdcomp_ref PUBLIC -msse4.1)
    target_link_libraries(bitpack_bench PRIVATE simdcomp_ref)
    target_compile_definitions(bitpack_bench PRIVATE BITPACK_BENCH_REFERENCE=1)
  endif()
endif()
