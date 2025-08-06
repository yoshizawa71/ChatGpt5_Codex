set(srcs 
         "src/mqtt_tcp.c"
        
      )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES driver
                             esp_event
                             esp_netif
                             datalogger-control
                             mqtt
                             )

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 