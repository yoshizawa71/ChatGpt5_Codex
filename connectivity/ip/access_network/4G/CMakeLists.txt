
# Fontes do componente
set(srcs 
        "src/4G_system_control.c" 
        "src/sara_r422_cfg.c" 
        "src/u_cell_http_connection.c"
        "src/u_cell_information.c"
        "src/u_cell_mqtt.c"
        "src/u_cell_net_reg.c"
        "src/u_cell_power_strategy.c"
        "src/ppp_control.c"
        "src/u_cell_sms.c"
        "src/connectivity.c"   
      )

# Registro do componente
idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS src include
                    REQUIRES driver
                             esp_event
                             esp_netif
                             ubxlib
                             datalogger-control
                             main
                             )                         

target_compile_definitions(${COMPONENT_LIB} PRIVATE USE_AT_CONNECT)
# Flags de compilação extras
 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 