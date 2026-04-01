idf_component_register(
    SRCS "input_manager.c" "button.c" "encoder.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_pcnt event_bus debug_log log freertos
)
