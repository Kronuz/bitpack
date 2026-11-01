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
  target_compile_definitions(bitpack_test PRIVATE BITPACK_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME bitpack COMMAND bitpack_test)
  add_executable(bitpack_portable_test test/test.cc)
  target_link_libraries(bitpack_portable_test PRIVATE bitpack)
  target_compile_definitions(bitpack_portable_test PRIVATE BITPACK_NO_SIMD BITPACK_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME bitpack_portable COMMAND bitpack_portable_test)
  # Live kernels against a frozen copy of the previous ones (SIMD and portable builds).
  add_executable(bitpack_differential_test test/differential.cc)
  target_link_libraries(bitpack_differential_test PRIVATE bitpack)
  target_include_directories(bitpack_differential_test PRIVATE test)
  add_test(NAME bitpack_differential COMMAND bitpack_differential_test)
  add_executable(bitpack_differential_portable_test test/differential.cc)
  target_link_libraries(bitpack_differential_portable_test PRIVATE bitpack)
  target_include_directories(bitpack_differential_portable_test PRIVATE test)
  target_compile_definitions(bitpack_differential_portable_test PRIVATE BITPACK_NO_SIMD)
  add_test(NAME bitpack_differential_portable COMMAND bitpack_differential_portable_test)

  # libFuzzer target (Clang): cmake -DBITPACK_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++
  option(BITPACK_FUZZ "Build the libFuzzer target" OFF)
  if(BITPACK_FUZZ)
    add_executable(bitpack_fuzz fuzz/fuzz_decode.cc)
    target_link_libraries(bitpack_fuzz PRIVATE bitpack)
    target_compile_options(bitpack_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(bitpack_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()

  option(BITPACK_BENCH_REFERENCE "Fetch Lemire's SIMD-BP128 (simdcomp on AArch64) as a benchmark-only reference" OFF)
  add_executable(bitpack_bench bench/bench.cc)
  target_link_libraries(bitpack_bench PRIVATE bitpack)
  if(BITPACK_BENCH_REFERENCE)
    # Benchmark-only: never part of the bitpack target.
    include(FetchContent)
    if(POLICY CMP0169)
      cmake_policy(SET CMP0169 OLD)  # populate only; the reference is not a CMake subproject here
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|ARM64|aarch64)$")
      # simdcomp (BSD-3) ships a NEON shim; -DFETCHCONTENT_SOURCE_DIR_SIMDCOMP_REF=<dir> uses a local copy.
      enable_language(C)
      FetchContent_Declare(simdcomp_ref
        GIT_REPOSITORY https://github.com/lemire/simdcomp.git
        GIT_TAG        d5301778fe5045ca8099252de5a6ed8016a287df)
      FetchContent_Populate(simdcomp_ref)
      add_library(simdcomp_ref STATIC
        ${simdcomp_ref_SOURCE_DIR}/src/simdbitpacking.c
        ${simdcomp_ref_SOURCE_DIR}/src/simdintegratedbitpacking.c
        ${simdcomp_ref_SOURCE_DIR}/src/simdcomputil.c)
      target_compile_options(simdcomp_ref PRIVATE -O3)
      target_compile_definitions(bitpack_bench PRIVATE BITPACK_BENCH_SIMDCOMP=1)
    else()
      # SIMD-BP128 (Apache-2.0)
      FetchContent_Declare(simdcomp_ref
        GIT_REPOSITORY https://github.com/lemire/SIMDCompressionAndIntersection.git
        GIT_TAG        b809f38b57fa65384166c7c1d83056395a1ff3e5)  # the commit the README numbers use
      FetchContent_Populate(simdcomp_ref)
      add_library(simdcomp_ref STATIC
        ${simdcomp_ref_SOURCE_DIR}/src/simdbitpacking.cpp
        ${simdcomp_ref_SOURCE_DIR}/src/simdintegratedbitpacking.cpp
        ${simdcomp_ref_SOURCE_DIR}/src/usimdbitpacking.cpp)
      target_compile_options(simdcomp_ref PUBLIC -msse4.1)
    endif()
    target_include_directories(simdcomp_ref PUBLIC ${simdcomp_ref_SOURCE_DIR}/include)
    target_link_libraries(bitpack_bench PRIVATE simdcomp_ref)
    target_compile_definitions(bitpack_bench PRIVATE BITPACK_BENCH_REFERENCE=1)
  endif()
endif()
