# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.8)

set(EXTRA_COMPONENT_DIRS "../.." "../../../mbedtls_cxx")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ota_test)
