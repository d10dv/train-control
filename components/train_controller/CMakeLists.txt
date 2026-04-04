idf_component_register(
    SRCS "train_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES event_bus mqtt_broker display debug_log log esp_event
)
