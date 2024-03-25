cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(common_component_dir ../../../../common_components)
set(EXTRA_COMPONENT_DIRS
   ../../..
   ../../../esp_websocket_client
  "${common_component_dir}/linux_compat/esp_timer"
  "${common_component_dir}/linux_compat"
  "${common_component_dir}/linux_compat/freertos")

list(APPEND EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/protocols/linux_stubs/esp_stubs)

set(COMPONENTS esp_websocket_client main)
project(websocket)

idf_component_get_property(esp_websocket_client esp_websocket_client COMPONENT_LIB)
target_compile_definitions(${esp_websocket_client} PRIVATE "-DCONFIG_IDF_TARGET_LINUX")
target_link_options(${esp_websocket_client} INTERFACE -fsanitize=address -fsanitize=undefined)
