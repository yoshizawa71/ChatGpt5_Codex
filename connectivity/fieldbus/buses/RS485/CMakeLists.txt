
set(srcs "src/rs485_hw.c"
         "src/rs485_manager.c"
         "src/rs485_registry.c"
         "src/rs485_registry_adapter.c"
         "src/rs485_central.c"
         "src/rs485_sd_adapter.c"
      )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS include
                    REQUIRES freertos
                             driver
                             esp_wifi
                             lwip
                             espressif__esp-modbus
                             nvs_flash
                             modbus
                             XY_MD02
                             JSY-MK-333
                             energy
                             datalogger-driver
                             )

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 