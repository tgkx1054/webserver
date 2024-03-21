cmake_minimum_required(VERSION 3.10)
project(webserver)

set(CMAKE_CXX_STANDARD 11)

add_executable(webserver main.cpp http_conn.cpp)

find_package(Threads)

target_link_libraries(webserver ${CMAKE_THREAD_LIBS_INIT})