# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_get_property(target IDF_TARGET)

set(EXTRA_COMPONENT_DIRS "..")
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
list(APPEND EXTRA_COMPONENT_DIRS "../../../common_components/protocol_examples_common")

if(${IDF_TARGET} STREQUAL "linux")
  set(common_component_dir ../../../common_components)
  set(EXTRA_COMPONENT_DIRS
     ".."
     "${common_component_dir}/linux_compat/esp_timer"
     "${common_component_dir}/linux_compat"
     "${common_component_dir}/linux_compat/freertos")

  list(APPEND EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)
  list(APPEND EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/protocols/linux_stubs/esp_stubs)
  set(COMPONENTS esp_websocket_client main)
endif()

project(websocket_example)
