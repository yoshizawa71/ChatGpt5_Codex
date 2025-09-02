set(srcs "src/log_mux.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS include
                    REQUIRES log driver
                             )

                            
