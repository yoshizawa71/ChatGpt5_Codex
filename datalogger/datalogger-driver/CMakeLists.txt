set(src_driver 
               "src/ads1015_reader.c"
               "src/config_driver.c"               
               "src/pcnt.c"
               "src/rs485.c"
               "src/sdcard_mmc.c"
               "src/server_comm.c"
               "src/TCA6408A.c"
               "src/timer.c" 
               "src/uart_modem.c"
               )

idf_component_register(SRCS "${src_driver}"
#                    INCLUDE_DIRS "include" "."
                    INCLUDE_DIRS src include
                    REQUIRES freertos modem driver esp_littlefs 
                            json sdmmc fatfs esp_http_client esp-tls
                            datalogger-control esp_timer esp_wifi unity i2c
                            soc energy system esp_littlefs vfs)
                            
 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 