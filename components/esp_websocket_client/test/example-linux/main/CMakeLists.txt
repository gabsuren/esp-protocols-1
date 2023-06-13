idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS
                    "."
                    REQUIRES esp_websocket_client)
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")

if(CONFIG_GCOV_ENABLED)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC  --coverage -fprofile-arcs -ftest-coverage)

    idf_component_get_property(websocket websocket COMPONENT_LIB)
    target_compile_options(${websocket} PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(${websocket} PUBLIC  --coverage -fprofile-arcs -ftest-coverage)
endif()
