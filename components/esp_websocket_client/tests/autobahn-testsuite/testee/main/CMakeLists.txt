set(requires_list
    esp_websocket_client
    protocol_examples_common
    esp_event
    console
    esp_timer
)

if(${IDF_TARGET} STREQUAL "linux")
    list(APPEND requires_list esp_netif esp_stubs)
else()
    list(APPEND requires_list nvs_flash esp_wifi)
endif()

idf_component_register(SRCS "autobahn_testee.c"
                       INCLUDE_DIRS "."
                       REQUIRES ${requires_list})
