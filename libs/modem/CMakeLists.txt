set(srcs "src/esp_modem.c"
        "src/esp_modem_dce_service"
        "src/esp_modem_netif.c"
        "src/esp_modem_compat.c"
        )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES driver
                             esp_event
                             esp_netif
                             ubxlib
                             )

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 