set(srcs "src/images.c" "src/screen_view.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES ssd1306
                             oled_display
                             driver datalogger-control
                             )

                            
