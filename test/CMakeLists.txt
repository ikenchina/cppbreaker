cmake_minimum_required (VERSION 2.8)

project(cppbreaker)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -Wall")

find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

include_directories(${GTEST_INCLUDE_DIRS} ../)

add_executable(cppbreaker ../circuit_breaker_test.cc ../../circuit_breaker.cc)

target_link_libraries(cppbreaker ${GTEST_BOTH_LIBRARIES})
target_link_libraries(cppbreaker ${CMAKE_THREAD_LIBS_INIT})
add_test(Test cppbreaker)
enable_testing()
