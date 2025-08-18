
idf_build_get_property(project_dir PROJECT_DIR)
set(REQUIRES driver)
idf_component_register(SRCS "main.c" "main_control.c"
                    INCLUDE_DIRS include "."
                    REQUIRES datalogger-driver
                             datalogger-control
                             modem
                             4G
                             WIFI
                             esp_littlefs
                             ubxlib
                             ssd1306
                             i2c
                             oled_display
                             pressure
                             rele
                             tcp_log_console
                             system
                             )
                             
set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../esp_web_server")
littlefs_create_partition_image(esp_web_server ${WEB_SRC_DIR} FLASH_IN_PROJECT)

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 