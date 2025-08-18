# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Espressif/frameworks/esp-idf-v5.3.1/components/ulp/cmake"
  "C:/ESP_IA/WorkSpace_Test/smart_iot_platform/build/esp-idf/datalogger-control/ulp_datalogger-control"
  "C:/ESP_IA/WorkSpace_Test/smart_iot_platform/build/esp-idf/datalogger-control/ulp_datalogger-control-prefix"
  "C:/ESP_IA/WorkSpace_Test/smart_iot_platform/build/esp-idf/datalogger-control/ulp_datalogger-control-prefix/tmp"
  "C:/ESP_IA/WorkSpace_Test/smart_iot_platform/build/esp-idf/datalogger-control/ulp_datalogger-control-prefix/src/ulp_datalogger-control-stamp"
  "C:/ESP_IA/WorkSpace_Test/smart_iot_platform/build/esp-idf/datalogger-control/ulp_datalogger-control-prefix/src"
  "C:/ESP_IA/WorkSpace_Test/smart_iot_platform/build/esp-idf/datalogger-control/ulp_datalogger-control-prefix/src/ulp_datalogger-control-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/ESP_IA/WorkSpace_Test/smart_iot_platform/build/esp-idf/datalogger-control/ulp_datalogger-control-prefix/src/ulp_datalogger-control-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/ESP_IA/WorkSpace_Test/smart_iot_platform/build/esp-idf/datalogger-control/ulp_datalogger-control-prefix/src/ulp_datalogger-control-stamp${cfgdir}") # cfgdir has leading slash
endif()
