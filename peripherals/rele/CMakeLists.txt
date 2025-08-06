set(srcs "src/rele.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES ssd1306
                             oled_display
                             driver
                             datalogger-driver
                             datalogger-control
                             )

                            
