idf_component_register(
    SRCS
        "src/payload_builder.c"
        "src/payload_time.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        json
        datalogger-control
        datalogger-driver
        system
        log
)
component_compile_options(-Wno-error=format= -Wno-format)
