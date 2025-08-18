set(srcs     "src/temp_hum_simple.c"
             "src/temperature_rs485.c"
             "src/temperature_meter.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "include"
                    REQUIRES driver
                             RS485
                             datalogger-driver
                             )

                            
