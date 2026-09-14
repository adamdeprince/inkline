find_package(Git REQUIRED)
foreach(_name ghostty-arm32-seek ghostty-linux-libc ghostty-inkline-retention)
set(_patch "${CMAKE_CURRENT_LIST_DIR}/../patches/${_name}.patch")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${_patch}"
    WORKING_DIRECTORY "${ghostty_SOURCE_DIR}"
    RESULT_VARIABLE _already_applied OUTPUT_QUIET ERROR_QUIET
)
if(NOT _already_applied EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check "${_patch}"
        WORKING_DIRECTORY "${ghostty_SOURCE_DIR}"
        RESULT_VARIABLE _check ERROR_VARIABLE _error
    )
    if(NOT _check EQUAL 0)
        message(FATAL_ERROR "The ${_name} patch does not match the Ghostty source: ${_error}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${_patch}"
        WORKING_DIRECTORY "${ghostty_SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
endforeach()
