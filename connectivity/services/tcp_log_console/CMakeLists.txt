
idf_component_register(
  SRCS "src/tcp_log_server.c"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES lwip
                esp_http_server
                esp_netif
)
                    
# Flags de compilação extras
 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 