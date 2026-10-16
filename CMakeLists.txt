cmake_minimum_required(VERSION 3.20)
project(cpp_fixed_point CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)  # __int128

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(fxp INTERFACE)
target_include_directories(fxp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(fxp INTERFACE -Wall -Wextra)

add_executable(fxp_tests tests/test_main.cpp tests/test_quantize.cpp)
target_link_libraries(fxp_tests PRIVATE fxp)

enable_testing()
add_test(NAME fxp_tests COMMAND fxp_tests)
