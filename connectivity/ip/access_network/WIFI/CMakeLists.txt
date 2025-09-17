set(srcs "src/wifi_softap_sta.c"
         "src/wifi_diag.c"
      )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES driver
                             esp_event
                             esp_netif
                             datalogger-control
                             mqtt
                             esp_wifi
                             nvs_flash
                             time_sync_sntp
                             )

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 