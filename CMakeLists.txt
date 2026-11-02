cmake_minimum_required(VERSION 3.20)

foreach(X_ENV_VAR
    X_PROJECT_NAME
    X_TARGET_BIN_NAME
    X_BUILD_ID
    X_BUILD_TYPE
    X_WINDOW_NAME
    X_WINDOW_LOGO_PATH
    XP_DEV_FEATURES_ENABLED
    XP_EXPERIMENTAL_FEATURES_ENABLED
)
    set(${X_ENV_VAR} "$ENV{${X_ENV_VAR}}")
endforeach()

if(X_BUILD_TYPE STREQUAL "RELEASE")
    set(CMAKE_BUILD_TYPE Release)
elseif(X_BUILD_TYPE STREQUAL "DEBUG")
    set(CMAKE_BUILD_TYPE Debug)
else()
    message(FATAL_ERROR "X_BUILD_TYPE must be RELEASE or DEBUG, got: '${X_BUILD_TYPE}'. Project/XEnv variables must be in the environment.")
endif()

project(XPlayerProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(CREATE_LINK
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
    COPY_ON_ERROR SYMBOLIC
)

add_subdirectory(Source)
