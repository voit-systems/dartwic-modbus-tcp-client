cmake_minimum_required(VERSION 3.30)
project(modbus_tcp_client)

set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
endif()

set(ENGINE_DIR "${PROJECT_SOURCE_DIR}/engine")
set(PLUGIN_MANIFEST_FILE "${PROJECT_SOURCE_DIR}/plugin.json")
set(BUILD_CONFIGURATION_FILE "${PROJECT_SOURCE_DIR}/build-configuration.json")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${PLUGIN_MANIFEST_FILE}"
        "${BUILD_CONFIGURATION_FILE}")

file(READ "${PLUGIN_MANIFEST_FILE}" PLUGIN_MANIFEST_JSON)
string(JSON ENGINE_PLUGIN_ID GET "${PLUGIN_MANIFEST_JSON}" id)
string(JSON ENGINE_PLUGINS_API_VERSION GET "${PLUGIN_MANIFEST_JSON}" engine_plugins_api_version)
string(JSON CONTAINS_ENGINE_PLUGIN GET "${PLUGIN_MANIFEST_JSON}" contains_engine_plugin)
if(NOT ENGINE_PLUGINS_API_VERSION MATCHES "^[0-9]+$")
    message(FATAL_ERROR "plugin.json engine_plugins_api_version must be an integer.")
endif()
if(NOT CONTAINS_ENGINE_PLUGIN)
    message(FATAL_ERROR "plugin.json must set contains_engine_plugin to true.")
endif()

file(READ "${BUILD_CONFIGURATION_FILE}" BUILD_CONFIGURATION_JSON)
string(JSON COPY_PLUGIN_VALUE ERROR_VARIABLE COPY_PLUGIN_ERROR GET "${BUILD_CONFIGURATION_JSON}" copy_plugin)
if(COPY_PLUGIN_ERROR)
    set(COPY_PLUGIN_VALUE true)
endif()

string(JSON EXTERNAL_ENGINE_ROOT ERROR_VARIABLE ENGINE_DIR_ERROR GET "${BUILD_CONFIGURATION_JSON}" engine_dir)
if(ENGINE_DIR_ERROR)
    set(EXTERNAL_ENGINE_ROOT "")
endif()

set(SHOULD_COPY_PLUGIN OFF)
if(COPY_PLUGIN_VALUE)
    set(SHOULD_COPY_PLUGIN ON)
endif()

set(ENGINE_PLUGIN_DIR "${PROJECT_SOURCE_DIR}/plugin/engine/${ENGINE_PLUGIN_ID}")

file(GLOB_RECURSE SRC_SOURCES "${ENGINE_DIR}/src/*.cpp")
add_library(modbus_tcp_client_engine SHARED ${SRC_SOURCES})

file(GLOB ENGINE_INCLUDE_DIRS
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES true
        "${ENGINE_DIR}/include/*")

target_include_directories(modbus_tcp_client_engine PRIVATE
        "${ENGINE_DIR}/include"
        ${ENGINE_INCLUDE_DIRS})

find_package(libmodbus CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(modbus_tcp_client_engine PRIVATE
        modbus
        nlohmann_json::nlohmann_json
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set_target_properties(modbus_tcp_client_engine PROPERTIES
        OUTPUT_NAME "${ENGINE_PLUGIN_ID}"
        RUNTIME_OUTPUT_DIRECTORY "${ENGINE_PLUGIN_DIR}/bin")

set(ENGINE_PLUGIN_COPY_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_PLUGIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PLUGIN_MANIFEST_FILE}"
        "${ENGINE_PLUGIN_DIR}/plugin.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ENGINE_DIR}/src/module_config.json"
        "${ENGINE_PLUGIN_DIR}/module_config.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ENGINE_DIR}/src/default_parameters.json"
        "${ENGINE_PLUGIN_DIR}/default_parameters.json")

if(SHOULD_COPY_PLUGIN AND NOT "${EXTERNAL_ENGINE_ROOT}" STREQUAL "")
    set(EXTERNAL_ENGINE_PLUGIN_DIR "${EXTERNAL_ENGINE_ROOT}/plugins/${ENGINE_PLUGIN_ID}")
    list(APPEND ENGINE_PLUGIN_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E make_directory "${EXTERNAL_ENGINE_ROOT}/plugins"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${ENGINE_PLUGIN_DIR}" "${EXTERNAL_ENGINE_PLUGIN_DIR}")
endif()

add_custom_target(modbus_tcp_client ALL
        ${ENGINE_PLUGIN_COPY_COMMANDS}
        DEPENDS modbus_tcp_client_engine
        COMMENT "Copying engine plugin files")

add_custom_target(copy_engine_plugin
        DEPENDS modbus_tcp_client)
