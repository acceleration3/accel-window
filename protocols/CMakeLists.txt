project(wayland-protocols LANGUAGES C)

add_custom_command(OUTPUT 
    "${PROJECT_SOURCE_DIR}/src/xdg-shell.c"
    "${PROJECT_SOURCE_DIR}/src/xdg-decoration.c"
    COMMAND ${WaylandScanner_EXECUTABLE} private-code ${WaylandProtocols_DATADIR}/stable/xdg-shell/xdg-shell.xml ${PROJECT_SOURCE_DIR}/src/xdg-shell.c
    COMMAND ${WaylandScanner_EXECUTABLE} client-header ${WaylandProtocols_DATADIR}/stable/xdg-shell/xdg-shell.xml ${PROJECT_SOURCE_DIR}/include/xdg-shell.h
    COMMAND ${WaylandScanner_EXECUTABLE} private-code ${WaylandProtocols_DATADIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml ${PROJECT_SOURCE_DIR}/src/xdg-decoration.c
    COMMAND ${WaylandScanner_EXECUTABLE} client-header ${WaylandProtocols_DATADIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml ${PROJECT_SOURCE_DIR}/include/xdg-decoration.h
)

add_library(wayland-protocols 
    "${PROJECT_SOURCE_DIR}/src/xdg-shell.c"
    "${PROJECT_SOURCE_DIR}/src/xdg-decoration.c")
target_include_directories(wayland-protocols PUBLIC include)