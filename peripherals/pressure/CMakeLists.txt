set(srcs "src/pressure_meter.c" "src/pressure_calibrate.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES ssd1306
                             oled_display
                             driver
                             datalogger-driver
                             )

                            
