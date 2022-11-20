project(external)

add_subdirectory(accel-macros)
add_subdirectory(accel-flagset)
add_subdirectory(accel-math)

list(APPEND ADDITIONAL_LIBRARIES accel-macros accel-math accel-flagset)
list(APPEND ADDITIONAL_INCLUDES ${GLM_INCLUDE_DIRS})

set(ADDITIONAL_LIBRARIES ${ADDITIONAL_LIBRARIES} PARENT_SCOPE)
set(ADDITIONAL_INCLUDES ${ADDITIONAL_INCLUDES} PARENT_SCOPE)