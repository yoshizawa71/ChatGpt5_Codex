set(srcs "src/system.c"
         "src/filesystem.c"
         "src/adaptive_delay.c"
         "src/reboot_test.c"
         )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS include
                    REQUIRES esp_littlefs
                              freertos
                              nvs_flash
                              esp_pm
                              esp_netif
                             )

                            
