cmake_minimum_required(VERSION 2.8.11)

project(dattobd C)

set(CMAKE_C_FLAGS "-std=gnu99 -Wall -fPIC")

set(src libdattobd.c)

add_library(dattobd STATIC ${src})
target_include_directories(dattobd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
