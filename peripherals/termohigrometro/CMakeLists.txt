set(srcs     "src/termohigro_meter.c"
             )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "include"
                    REQUIRES driver
                             RS485
                             datalogger-driver
                             )

                            
