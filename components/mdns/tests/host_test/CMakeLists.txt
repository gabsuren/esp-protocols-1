cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS    # Add esp_modem component and linux port components
         ../..)

set(COMPONENTS main)
project(mdns_host_test)
