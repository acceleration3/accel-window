project(tests CXX)

file(GLOB TEST_FILES "*.cpp")
foreach(FILE ${TEST_FILES})
    get_filename_component(TEST_NAME ${FILE} NAME_WE)
    message("Test found: ${TEST_NAME}, File: ${FILE}")
    add_executable(${TEST_NAME} ${FILE} ${ADDITIONAL_SOURCES})
    target_link_libraries(${TEST_NAME} PUBLIC accel-window)
endforeach()