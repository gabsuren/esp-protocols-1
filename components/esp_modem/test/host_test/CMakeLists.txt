cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS    # Add esp_modem component and linux port components
        ../..
        ../../port/linux)
set(EXCLUDE_COMPONENTS log)
#        "$ENV{IDF_PATH}/tools/mocks/freertos/")

idf_build_set_property(__BUILD_COMPONENT_DEPGRAPH_ENABLED 1)

set(COMPONENTS esp_modem main)
project(host_modem_test)

idf_component_get_property(esp_modem esp_modem COMPONENT_LIB)
target_compile_definitions(${esp_modem} PRIVATE "-DCONFIG_COMPILER_CXX_EXCEPTIONS")
target_compile_definitions(${esp_modem} PRIVATE "-DCONFIG_IDF_TARGET_LINUX")
target_link_options(${esp_modem} INTERFACE -fsanitize=address -fsanitize=undefined)
