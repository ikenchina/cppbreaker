cmake_minimum_required (VERSION 2.8)

project(cppbreaker_demo)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -Wall")

include_directories(${GTEST_INCLUDE_DIRS} ../)

add_executable(cppbreaker_demo ../demo.cc ../../circuit_breaker.cc)
