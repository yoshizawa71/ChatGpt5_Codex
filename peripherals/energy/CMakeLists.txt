set(srcs "src/energy_meter.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES ssd1306
                             oled_display
                             driver
                             )

                            
