idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES event_bus debug_log wifi_ap mqtt_broker input_manager nvs_flash esp_event esp_netif log
)
