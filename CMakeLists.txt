cmake_minimum_required(VERSION 3.30)
project(modbus_tcp_client)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
endif()

set(ENGINE_DIR "${PROJECT_SOURCE_DIR}/engine")
set(PLUGIN_MANIFEST_FILE "${PROJECT_SOURCE_DIR}/plugin.json")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${PLUGIN_MANIFEST_FILE}")

file(READ "${PLUGIN_MANIFEST_FILE}" PLUGIN_MANIFEST_JSON)
string(JSON ENGINE_PLUGIN_ID GET "${PLUGIN_MANIFEST_JSON}" id)
string(JSON MIN_ENGINE_VERSION GET "${PLUGIN_MANIFEST_JSON}" minEngineVersion)
string(JSON CONTAINS_ENGINE_PLUGIN GET "${PLUGIN_MANIFEST_JSON}" contains_engine_plugin)
if("${MIN_ENGINE_VERSION}" STREQUAL "")
    message(FATAL_ERROR "plugin.json must set minEngineVersion.")
endif()
if(NOT CONTAINS_ENGINE_PLUGIN)
    message(FATAL_ERROR "plugin.json must set contains_engine_plugin to true.")
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" NORMALIZED_BUILD_TYPE)
set(ENGINE_PLUGIN_VARIANT "engine")
if(NORMALIZED_BUILD_TYPE STREQUAL "debug")
    set(ENGINE_PLUGIN_VARIANT "engine-debug")
endif()

set(ENGINE_PLUGIN_DIR "${PROJECT_SOURCE_DIR}/plugin/${ENGINE_PLUGIN_VARIANT}/${ENGINE_PLUGIN_ID}")

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

if(WIN32)
    target_link_libraries(modbus_tcp_client_engine PRIVATE iphlpapi ws2_32)
endif()

set_target_properties(modbus_tcp_client_engine PROPERTIES
        OUTPUT_NAME "${ENGINE_PLUGIN_ID}"
        RUNTIME_OUTPUT_DIRECTORY "${ENGINE_PLUGIN_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${ENGINE_PLUGIN_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${ENGINE_PLUGIN_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${ENGINE_PLUGIN_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${ENGINE_PLUGIN_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${ENGINE_PLUGIN_DIR}/bin"
        PDB_OUTPUT_DIRECTORY "${ENGINE_PLUGIN_DIR}/bin"
        PDB_OUTPUT_DIRECTORY_DEBUG "${ENGINE_PLUGIN_DIR}/bin"
        PDB_OUTPUT_DIRECTORY_RELEASE "${ENGINE_PLUGIN_DIR}/bin")

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

add_custom_target(modbus_tcp_client ALL
        ${ENGINE_PLUGIN_COPY_COMMANDS}
        DEPENDS modbus_tcp_client_engine
        COMMENT "Copying engine plugin files")

add_custom_target(copy_engine_plugin
        DEPENDS modbus_tcp_client)

if(BUILD_TESTING)
    add_executable(modbus_read_write_integration
            tests/modbus_read_write_integration.cpp
            engine/src/modbus_tcp_client.cpp
            engine/src/modbus_tcp_client_module.cpp
            engine/src/modbus_device_discovery.cpp
            engine/src/modbus_register_scanner.cpp)
    target_include_directories(modbus_read_write_integration PRIVATE
            "${ENGINE_DIR}/include"
            ${ENGINE_INCLUDE_DIRS})
    target_link_libraries(modbus_read_write_integration PRIVATE
            modbus
            nlohmann_json::nlohmann_json)
    if(WIN32)
        target_link_libraries(modbus_read_write_integration PRIVATE iphlpapi ws2_32)
    endif()
    add_test(NAME modbus_read_write_integration COMMAND modbus_read_write_integration)

    add_executable(modbus_live_probe tests/modbus_live_probe.cpp)
    target_link_libraries(modbus_live_probe PRIVATE modbus)

    add_executable(modbus_register_scan_integration
            tests/modbus_register_scan_integration.cpp
            engine/src/modbus_register_scanner.cpp)
    target_include_directories(modbus_register_scan_integration PRIVATE
            "${ENGINE_DIR}/include")
    target_link_libraries(modbus_register_scan_integration PRIVATE
            modbus
            nlohmann_json::nlohmann_json)
    add_test(NAME modbus_register_scan_integration COMMAND modbus_register_scan_integration)
endif()

message(STATUS "Modbus engine plugin dir: ${ENGINE_PLUGIN_DIR}")
message(STATUS "Modbus engine plugin variant: ${ENGINE_PLUGIN_VARIANT}")
