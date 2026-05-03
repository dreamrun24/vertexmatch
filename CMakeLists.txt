cmake_minimum_required(VERSION 3.16)
project(VertexMatch VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VERTEXMATCH_ENABLE_NATIVE "Enable -march=native for local benchmarking" ON)
option(VERTEXMATCH_ENABLE_WARNINGS "Enable compiler warnings" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(vertexmatch
  src/baseline_book.cpp
  src/optimized_book.cpp
  src/csv.cpp
  src/itch.cpp
  src/generator.cpp
  src/metrics.cpp
  src/rdtsc.cpp
  src/selftest.cpp
)

target_include_directories(vertexmatch PUBLIC include)

if(MSVC)
  target_compile_options(vertexmatch PRIVATE /O2)
  if(VERTEXMATCH_ENABLE_WARNINGS)
    target_compile_options(vertexmatch PRIVATE /W4)
  endif()
else()
  target_compile_options(vertexmatch PRIVATE -O3 -fno-omit-frame-pointer)
  if(VERTEXMATCH_ENABLE_NATIVE)
    target_compile_options(vertexmatch PRIVATE -march=native)
  endif()
  if(VERTEXMATCH_ENABLE_WARNINGS)
    target_compile_options(vertexmatch PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

add_executable(vertexmatch_cli src/main.cpp)
target_link_libraries(vertexmatch_cli PRIVATE vertexmatch)

find_package(benchmark QUIET)
if(benchmark_FOUND)
  add_executable(vertexmatch_google_bench benchmarks/google_bench.cpp)
  target_link_libraries(vertexmatch_google_bench PRIVATE vertexmatch benchmark::benchmark)
endif()

enable_testing()
add_test(NAME selftest COMMAND vertexmatch_cli selftest)
