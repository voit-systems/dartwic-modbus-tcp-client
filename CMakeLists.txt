cmake_minimum_required(VERSION 3.30)
project(modbus_tcp_client)

set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
endif()

set(ENGINE_DIR "${PROJECT_SOURCE_DIR}/engine")
set(PACKAGE_INFO_FILE "${PROJECT_SOURCE_DIR}/package-info.json")
set(BUILD_CONFIGURATION_FILE "${PROJECT_SOURCE_DIR}/build-configuration.json")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${PACKAGE_INFO_FILE}"
        "${ENGINE_DIR}/plugin.json"
        "${BUILD_CONFIGURATION_FILE}")

file(READ "${ENGINE_DIR}/plugin.json" ENGINE_PLUGIN_JSON)
string(JSON ENGINE_PLUGIN_ID GET "${ENGINE_PLUGIN_JSON}" id)
string(JSON ENGINE_PLUGINS_API_VERSION GET "${ENGINE_PLUGIN_JSON}" plugins_api_version)
if(NOT ENGINE_PLUGINS_API_VERSION MATCHES "^[0-9]+$")
    message(FATAL_ERROR "engine/plugin.json plugins_api_version must be an integer.")
endif()
file(READ "${PACKAGE_INFO_FILE}" PACKAGE_INFO_JSON)
string(JSON PACKAGE_INFO_JSON SET "${PACKAGE_INFO_JSON}" "plugins_api_version" "${ENGINE_PLUGINS_API_VERSION}")
string(JSON PACKAGE_INFO_JSON SET "${PACKAGE_INFO_JSON}" "contains_engine_plugin" true)
file(WRITE "${PACKAGE_INFO_FILE}" "${PACKAGE_INFO_JSON}\n")

file(READ "${BUILD_CONFIGURATION_FILE}" BUILD_CONFIGURATION_JSON)
string(JSON COPY_PACKAGE_VALUE ERROR_VARIABLE COPY_PACKAGE_ERROR GET "${BUILD_CONFIGURATION_JSON}" copy_package)
if(COPY_PACKAGE_ERROR)
    set(COPY_PACKAGE_VALUE true)
endif()

string(JSON EXTERNAL_ENGINE_ROOT ERROR_VARIABLE ENGINE_DIR_ERROR GET "${BUILD_CONFIGURATION_JSON}" engine_dir)
if(ENGINE_DIR_ERROR)
    set(EXTERNAL_ENGINE_ROOT "")
endif()

set(SHOULD_COPY_PACKAGE OFF)
if(COPY_PACKAGE_VALUE)
    set(SHOULD_COPY_PACKAGE ON)
endif()

file(READ "${PACKAGE_INFO_FILE}" PACKAGE_INFO_JSON)
string(JSON PACKAGE_ID GET "${PACKAGE_INFO_JSON}" id)

set(ENGINE_PACKAGE_DIR "${PROJECT_SOURCE_DIR}/package/engine-plugin/${ENGINE_PLUGIN_ID}")

file(GLOB_RECURSE SRC_SOURCES "${ENGINE_DIR}/src/*.cpp")
add_library(modbus_tcp_client SHARED ${SRC_SOURCES})

file(GLOB ENGINE_INCLUDE_DIRS
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES true
        "${ENGINE_DIR}/include/*")

target_include_directories(modbus_tcp_client PRIVATE
        "${ENGINE_DIR}/include"
        ${ENGINE_INCLUDE_DIRS})

find_package(libmodbus CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(modbus_tcp_client PRIVATE
        modbus
        nlohmann_json::nlohmann_json
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set_target_properties(modbus_tcp_client PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ENGINE_PACKAGE_DIR}/bin")

add_custom_command(TARGET modbus_tcp_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ENGINE_DIR}/plugin.json"
        "${ENGINE_PACKAGE_DIR}/plugin.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ENGINE_DIR}/src/module_config.json"
        "${ENGINE_PACKAGE_DIR}/module_config.json"
        COMMENT "Copying engine plugin metadata into package output"
)

if(SHOULD_COPY_PACKAGE AND NOT "${EXTERNAL_ENGINE_ROOT}" STREQUAL "")
    set(EXTERNAL_ENGINE_PLUGIN_DIR "${EXTERNAL_ENGINE_ROOT}/plugins/${ENGINE_PLUGIN_ID}")
    add_custom_command(TARGET modbus_tcp_client POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${EXTERNAL_ENGINE_ROOT}/plugins"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${ENGINE_PACKAGE_DIR}" "${EXTERNAL_ENGINE_PLUGIN_DIR}"
            COMMENT "Copying packaged engine plugin to external DARTWIC engine plugins directory")
endif()
