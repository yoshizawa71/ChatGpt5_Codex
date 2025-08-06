
set(srcs 
         "src/modbus_control.c" "src/tcp_log_server.c" "src/modbus_master.c" "src/modbus_slaves.c"
        
      )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES freertos 
                             esp_wifi 
                             lwip
                             espressif__esp-modbus
                             nvs_flash
                             datalogger-driver
                             )

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 