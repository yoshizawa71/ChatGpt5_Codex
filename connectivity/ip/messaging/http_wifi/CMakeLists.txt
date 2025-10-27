idf_component_register(
    SRCS
        "src/http_client_esp.c"
        "src/http_publisher.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_http_client
        json
        esp_netif
        esp_event
        esp_wifi
        datalogger-control
        datalogger-driver
        system
        log
        payload_common
)

component_compile_options(-Wno-error=format= -Wno-format)