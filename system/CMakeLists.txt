set(srcs "src/system.c" "src/filesystem.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS include
                    REQUIRES datalogger-control
                             datalogger-driver
                             esp_littlefs
                             )

                            
