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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BUILD_CONFIGURATION_FILE}")

file(READ "${PACKAGE_INFO_FILE}" PACKAGE_INFO_JSON)
string(JSON PACKAGE_ID GET "${PACKAGE_INFO_JSON}" id)
file(READ "${BUILD_CONFIGURATION_FILE}" BUILD_CONFIGURATION_JSON)
set(COPY_PACKAGE TRUE)
string(JSON COPY_PACKAGE_VALUE ERROR_VARIABLE COPY_PACKAGE_ERROR GET "${BUILD_CONFIGURATION_JSON}" copy_package)
if(NOT COPY_PACKAGE_ERROR)
    if("${COPY_PACKAGE_VALUE}" STREQUAL "false" OR "${COPY_PACKAGE_VALUE}" STREQUAL "FALSE" OR "${COPY_PACKAGE_VALUE}" STREQUAL "OFF" OR "${COPY_PACKAGE_VALUE}" STREQUAL "0")
        set(COPY_PACKAGE FALSE)
    endif()
endif()
string(JSON ENGINE_INSTALL_DIR ERROR_VARIABLE ENGINE_INSTALL_DIR_ERROR GET "${BUILD_CONFIGURATION_JSON}" engine_dir)
if(NOT ENGINE_INSTALL_DIR_ERROR AND NOT "${ENGINE_INSTALL_DIR}" STREQUAL "" AND NOT "${ENGINE_INSTALL_DIR}" STREQUAL "null")
    file(TO_CMAKE_PATH "${ENGINE_INSTALL_DIR}" ENGINE_INSTALL_DIR)
endif()
set(ENGINE_PACKAGE_DIR "${PROJECT_SOURCE_DIR}/package/engine-module/${PACKAGE_ID}")

############### SOURCE FILES ###############
file(GLOB_RECURSE SRC_SOURCES "${ENGINE_DIR}/src/*.cpp")

### CREATE MAIN LIBRARY ###
add_library(modbus_tcp_client SHARED ${SRC_SOURCES})

############### INCLUDE DIRECTORIES ###############
file(GLOB subdirs
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES true
        "${ENGINE_DIR}/include/*")

target_include_directories(modbus_tcp_client PRIVATE
        "${ENGINE_DIR}/include"
        ${subdirs})

############### NON HEADER ONLY LIBRARY INCLUDES ###############
find_package(yaml-cpp CONFIG REQUIRED)
find_package(libmodbus CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(modbus_tcp_client PRIVATE
        yaml-cpp::yaml-cpp
        modbus
        nlohmann_json
)

############### OUTPUT DIRECTORIES ###############
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set_target_properties(modbus_tcp_client PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ENGINE_PACKAGE_DIR}/bin"
)

############### COPY FILES ###############
set(CONFIG_FILE "${ENGINE_DIR}/src/module_config.yaml")

add_custom_command(TARGET modbus_tcp_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CONFIG_FILE}"
        "${ENGINE_PACKAGE_DIR}/module_config.yaml"
        COMMENT "Copying module_config.yaml to output folder"
)

if(COPY_PACKAGE AND NOT ENGINE_INSTALL_DIR_ERROR AND NOT "${ENGINE_INSTALL_DIR}" STREQUAL "" AND NOT "${ENGINE_INSTALL_DIR}" STREQUAL "null")
    set(ENGINE_REGISTRY_DIR "${ENGINE_INSTALL_DIR}/modules/registry/${PACKAGE_ID}")

    add_custom_command(TARGET modbus_tcp_client POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_INSTALL_DIR}/modules/registry"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${ENGINE_PACKAGE_DIR}" "${ENGINE_REGISTRY_DIR}"
            COMMENT "Copying packaged engine module to external engine registry"
    )
endif()
