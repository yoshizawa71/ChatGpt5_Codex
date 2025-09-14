idf_component_register(
    SRCS "src/time_sync_sntp.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif lwip
)
component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 