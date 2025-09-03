
set(srcs "src/rs485_hw.c"
         "src/rs485_manager.c"
         "src/rs485_registry.c"  
      )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS include
                    REQUIRES freertos
                             driver 
                             esp_wifi 
                             lwip
                             espressif__esp-modbus
                             nvs_flash
                             termohigrometro
                             modbus
                             )

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 