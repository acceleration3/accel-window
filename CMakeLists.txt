cmake_minimum_required(VERSION 3.10)

project(accel-window CXX)

option(BUILD_TESTS "Build the accompanying tests." ON)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(external)

file(GLOB_RECURSE SRC_FILES "src/*.cpp" "include/accel/*.hpp")

add_library(accel-window-static STATIC ${SRC_FILES})
target_include_directories(accel-window-static PUBLIC "include/" ${ADDITIONAL_INCLUDES})
target_link_libraries(accel-window-static PUBLIC ${ADDITIONAL_LIBRARIES})

add_library(accel-window SHARED ${SRC_FILES})
target_compile_definitions(accel-window PUBLIC -DSHARED_BUILD PRIVATE -DACCEL_EXPORTS)
target_include_directories(accel-window PUBLIC "include/" ${ADDITIONAL_INCLUDES})
target_link_libraries(accel-window PUBLIC ${ADDITIONAL_LIBRARIES})

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()