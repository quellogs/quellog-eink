cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(QUELLOG_REPO_ROOT "${CMAKE_SOURCE_DIR}/..")
set(QUELLOG_WEB_UI_DIR "${QUELLOG_REPO_ROOT}/device-config-web")
set(QUELLOG_WEB_UI_INDEX "${QUELLOG_WEB_UI_DIR}/dist/index.html")
set(QUELLOG_WEB_UI_DONE "${QUELLOG_WEB_UI_DIR}/dist/done.html")

file(GLOB_RECURSE QUELLOG_WEB_UI_SOURCES CONFIGURE_DEPENDS
    "${QUELLOG_WEB_UI_DIR}/src/*"
    "${QUELLOG_WEB_UI_DIR}/scripts/*"
    "${QUELLOG_WEB_UI_DIR}/*.html"
    "${QUELLOG_WEB_UI_DIR}/package.json"
    "${QUELLOG_WEB_UI_DIR}/package-lock.json"
    "${QUELLOG_WEB_UI_DIR}/vite.config.js"
)

if(NOT EXISTS "${QUELLOG_WEB_UI_INDEX}" OR NOT EXISTS "${QUELLOG_WEB_UI_DONE}")
    message(STATUS "Building Quellog device-config-web bootstrap artifacts")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/build_web_ui.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE QUELLOG_WEB_UI_BOOTSTRAP_RESULT
    )
    if(NOT QUELLOG_WEB_UI_BOOTSTRAP_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to bootstrap device-config-web dist artifacts")
    endif()
endif()

add_custom_target(quellog_web_ui_build ALL
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/build_web_ui.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS ${QUELLOG_WEB_UI_SOURCES}
    USES_TERMINAL
    VERBATIM
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(quellog_eink_firmware)
