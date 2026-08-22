cmake_minimum_required(VERSION 3.14)
project(jni C)

set(CMAKE_C_STANDARD 11)

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(JNI_SOURCES src/kcjni.c lib/parson/parson.c)

add_library(jni SHARED ${JNI_SOURCES})

target_include_directories(jni PRIVATE src lib/parson)

if(ANDROID)
    target_link_libraries(jni dl)
endif()
