cmake_minimum_required(VERSION 3.10)

project(accel-window CXX)

option(BUILD_TESTS "Build the accompanying tests." ON)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(ACCEL_BUILD_TESTS ON)

if(NOT DEFINED ACCEL_MODULES_FOLDER)
    set(ACCEL_MODULES_FOLDER "${CMAKE_SOURCE_DIR}/external")
endif()

set(ADDITIONAL_LIBRARIES accel-macros accel-flagset accel-math)
set(ADDITIONAL_INCLUDES "")
set(ADDITIONAL_SOURCES "")

include(cmake/FindModule.cmake)
add_accel_module(accel-macros)
add_accel_module(accel-flagset)
add_accel_module(accel-math)

file(GLOB_RECURSE SRC_FILES "src/*.cpp" "include/accel/*.hpp")

add_library(accel-window INTERFACE)
target_include_directories(accel-window INTERFACE "include/" ${ADDITIONAL_INCLUDES})
target_link_libraries(accel-window INTERFACE ${ADDITIONAL_LIBRARIES})

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()