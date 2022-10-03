cmake_minimum_required(VERSION 3.22)
project(zppproto CXX)


include(FetchContent)


set(CMAKE_CXX_STANDARD 20)
enable_testing()
add_subdirectory(tests)