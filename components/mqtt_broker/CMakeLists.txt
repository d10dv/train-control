idf_component_register(
    SRCS "mqtt_broker.c"
    INCLUDE_DIRS "include"
    REQUIRES mongoose event_bus debug_log log freertos
)
