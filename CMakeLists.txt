cmake_minimum_required(VERSION 3.16)

project(accel-window CXX)

option(USE_X11 "Use X11 instead of wayland." OFF)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)

if(NOT DEFINED ACCEL_MODULES_FOLDER)
    set(ACCEL_MODULES_FOLDER "${CMAKE_SOURCE_DIR}/external")
endif()

set(ADDITIONAL_LIBRARIES accel-macros accel-flagset accel-utf8)
set(ADDITIONAL_INCLUDES "")
set(ADDITIONAL_SOURCES "")
set(ADDITIONAL_DEFINES "")

if(UNIX AND USE_X11)
    set(ADDITIONAL_DEFINES "USE_X11")
    list(APPEND ADDITIONAL_LIBRARIES X11)
else()
    find_package(ECM REQUIRED NO_MODULE)
    list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

    find_package(Wayland REQUIRED COMPONENTS Client)
    find_package(WaylandScanner REQUIRED)
    find_package(WaylandProtocols REQUIRED)

    add_subdirectory(protocols)

    list(APPEND ADDITIONAL_LIBRARIES Wayland::Client wayland-protocols)
endif()

include(cmake/FindModule.cmake)
add_accel_module(accel-macros)
add_accel_module(accel-flagset)
add_accel_module(accel-utf8)

file(GLOB_RECURSE SRC_FILES "src/*.cpp" "include/accel/*.hpp")

add_library(accel-window INTERFACE)
target_include_directories(accel-window INTERFACE "include/" ${ADDITIONAL_INCLUDES})
target_link_libraries(accel-window INTERFACE ${ADDITIONAL_LIBRARIES})
target_compile_definitions(accel-window INTERFACE ${ADDITIONAL_DEFINES})

if(ACCEL_BUILD_TESTS)
    add_subdirectory(tests)
endif()
