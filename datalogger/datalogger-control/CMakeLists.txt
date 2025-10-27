set(src_driver   
               "src/sleep_control.c"
               "src/sleep_preparation.c"
               "src/ota_control.c"
               "src/config_control.c"
               "src/factory_control.c"
               "src/pulse_meter.c"
               "src/wakeup_stub.c"
               "src/led_blink_control.c"
               "src/battery_monitor.c"
               )

set(reqs
    soc nvs_flash ulp driver
    app_update esp_http_client esp_https_ota
    datalogger-driver esp_http_server json mdns
    spiffs oled_display rele pressure esp_wifi system
    4G log_mux
)      

# Quando Modbus Serial estiver habilitado, dependa do componente RS485
# (ele já compila rs485_manager.c, rs485_hw.c etc.)
if(CONFIG_MODBUS_SERIAL_ENABLE)
    list(APPEND reqs RS485)
endif()         
   
idf_component_register(SRCS "${src_driver}"
                    INCLUDE_DIRS "include" "."
                    REQUIRES ${reqs}
                            )
 
                      

# ULP support additions to component CMakeLists.txt.
#
# 1. The ULP app name must be unique (if multiple components use ULP).
set(ulp_app_name ulp_${COMPONENT_NAME})
#
# 2. Specify all assembly source files.
#    Files should be placed into a separate directory (in this case, ulp/),
#    which should not be added to COMPONENT_SRCS.
set(ulp_s_sources "ulp/pulse_counter.S" "ulp/wake_up.S" "ulp/sleep_pulse_cnt.S")
#
# 3. List all the component source files which include automatically
#    generated ULP export file, ${ulp_app_name}.h:
set(ulp_exp_dep_srcs "src/sleep_control.c")
#
# 4. Call function to build ULP binary and embed in project using the argument
#    values above.
ulp_embed_binary(${ulp_app_name} "${ulp_s_sources}" "${ulp_exp_dep_srcs}")

 component_compile_options(-Wno-error=format= -Wno-format) #Evitar Format Error 