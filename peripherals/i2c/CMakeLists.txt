set(srcs "src/i2c_dev_master.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES ssd1306
                             driver
                             )

                            
