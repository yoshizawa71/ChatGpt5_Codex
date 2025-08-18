set(srcs "src/rele.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES freertos
                             driver
                             
                             )

                            
