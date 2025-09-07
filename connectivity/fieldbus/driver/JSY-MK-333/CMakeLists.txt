
idf_component_register( SRCS "src/energy_jsy_mk_333_driver.c"
                             
                        INCLUDE_DIRS "include"
                        REQUIRES modbus
)

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 