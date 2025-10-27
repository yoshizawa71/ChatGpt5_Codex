idf_component_register(
    SRCS
        "src/mqtt_tcp.c"
        "src/mqtt_client_esp.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        mqtt
        json
        esp_netif
        esp_event
        esp_wifi
        datalogger-control
        datalogger-driver
        system
        log
)

component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 