# CMake build for the C32 CPU emulator (alternative to the Makefile).
#
#   cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-cmake -j
#   ctest --test-dir build-cmake --output-on-failure
cmake_minimum_required(VERSION 3.14)
project(c32_cpu VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
endif()

add_library(c32 STATIC
  src/isa.cpp
  src/memory.cpp
  src/cpu.cpp
  src/assembler.cpp
  src/program.cpp
  src/disassembler.cpp)
target_include_directories(c32 PUBLIC include)

foreach(tool c32asm c32sim c32dis)
  add_executable(${tool} tools/${tool}.cpp)
  target_link_libraries(${tool} PRIVATE c32)
endforeach()

add_executable(c32tests tests/test_main.cpp)
target_link_libraries(c32tests PRIVATE c32)

enable_testing()
add_test(NAME unit_tests COMMAND c32tests)
add_test(NAME example_programs
         COMMAND ${CMAKE_SOURCE_DIR}/scripts/run_examples.sh $<TARGET_FILE:c32sim>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
