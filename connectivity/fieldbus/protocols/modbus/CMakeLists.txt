
idf_component_register( SRCS "src/modbus_rtu_master.c"
   #                          "src/modbus_slaves.c"
                        INCLUDE_DIRS "include"
                        REQUIRES espressif__esp-modbus
                                 driver 
                                 freertos
)

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 