include(FetchContent)

function(add_accel_module name)
    if(EXISTS ${ACCEL_MODULES_FOLDER}/${name})
        add_subdirectory(${ACCEL_MODULES_FOLDER}/${name})
    else()
        FetchContent_Declare(
            ${name}
            GIT_REPOSITORY "https://www.github.com/ootbgames/${name}.git"
            GIT_TAG "master"
        )
        FetchContent_MakeAvailable(${name})
    endif()
endfunction()
