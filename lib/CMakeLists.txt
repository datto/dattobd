# cmake control file for libdattobd
set(CMAKE_VERBOSE_MAKEFILE ON)

cmake_minimum_required(VERSION 2.8)

SET(CMAKE_BUILD_TYPE Release)

IF(DEFINED CMAKE_BUILD_TYPE)
   SET(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo")
   MESSAGE ("Using existing build type.")
ELSE()
   SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo")
   MESSAGE ("Setting build type.")
ENDIF()

MESSAGE ("Build type: ${CMAKE_BUILD_TYPE}")

project(dattobd)

set(PROJECT_SOURCE_DIR ".")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "." )

set(CMAKE_C_FLAGS_RELEASE "")

include_directories(".")

set (src libdattobd.c)

# the binary itself
add_library (dattobd STATIC ${src})

