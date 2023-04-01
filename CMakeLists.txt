cmake_minimum_required(VERSION 3.0)

project(mpmc CXX)

set(CMAKE_CXX_STANDARD 17)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

set(SOURCES src/network.cpp src/thread_pool.cpp)

add_library(mylib SHARED ${SOURCES})

set_target_properties(mylib PROPERTIES OUTPUT_NAME "mylib")

include_directories(include)

add_executable(main src/main.cpp)

target_link_libraries(main fmt mylib)