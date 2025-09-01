
set(srcs #"src/modbus_control.c"
       #  "src/modbus_master.c"
       #  "src/modbus_slaves.c"
        # "src/tcp_log_server.c" 
         "src/modbus_rtu_master.c"
         "src/rs485_hw.c"
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
                             )

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 