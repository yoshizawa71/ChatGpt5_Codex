# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0115 OLD)

cmake_minimum_required(VERSION 3.5)
# Definir macro para suporte PPP
add_compile_definitions(U_CFG_PPP_ENABLE)

# ——————————————————————————————————————————————————
# Força o U_CFG_OVERRIDE para TODAS as compilações
# ——————————————————————————————————————————————————
# (puxa o que já estava em CMAKE_C_FLAGS/CXX_FLAGS e acrescenta o define)
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -DU_CFG_OVERRIDE")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DU_CFG_OVERRIDE")

# ──── FORÇAR suporte a %f no printf/snprintf (snprintf("%.3f",…) funcione corretamente) ────
# isso injeta o símbolo _printf_float na linkagem de todo o executável
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -u _printf_float"
)

# List all extra component directories (outside of components/)
# Only point at folders that contain a component.mk or CMakeLists/idf_component.yml
set(EXTRA_COMPONENT_DIRS "")
list(APPEND EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/components/ssd1306
    ${CMAKE_SOURCE_DIR}/connectivity/fieldbus/buses/RS485
    ${CMAKE_SOURCE_DIR}/connectivity/fieldbus/protocols/modbus
    ${CMAKE_SOURCE_DIR}/connectivity/fieldbus/driver/JSY-MK-333
    ${CMAKE_SOURCE_DIR}/connectivity/fieldbus/driver/XY_MD02
    ${CMAKE_SOURCE_DIR}/connectivity/ip/access_network
    ${CMAKE_SOURCE_DIR}/connectivity/ip/messaging
    ${CMAKE_SOURCE_DIR}/connectivity/services/tcp_log_console
    ${CMAKE_SOURCE_DIR}/connectivity/services/log_mux
    ${CMAKE_SOURCE_DIR}/connectivity/services/time_sync_sntp
    ${CMAKE_SOURCE_DIR}/datalogger/datalogger-control
    ${CMAKE_SOURCE_DIR}/datalogger/datalogger-driver
    ${CMAKE_SOURCE_DIR}/libs/esp_littlefs
    ${CMAKE_SOURCE_DIR}/libs/ubxlib
    ${CMAKE_SOURCE_DIR}/libs/modem
    ${CMAKE_SOURCE_DIR}/peripherals/oled_display
    ${CMAKE_SOURCE_DIR}/peripherals/i2c
    ${CMAKE_SOURCE_DIR}/peripherals/energy
    ${CMAKE_SOURCE_DIR}/peripherals/pressure
    ${CMAKE_SOURCE_DIR}/peripherals/rele
    ${CMAKE_SOURCE_DIR}/peripherals/termohigrometro 
    ${CMAKE_SOURCE_DIR}/system

)

# Incluir ferramentas do ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Nome do projeto
project(smart_iot_platform)

# Obter diretório do projeto
idf_build_get_property(project_dir PROJECT_DIR)
