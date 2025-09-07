
idf_component_register( SRCS "src/xy_md02_driver.c"
                            
                        INCLUDE_DIRS "include"
                        REQUIRES modbus
)

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 