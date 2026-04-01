idf_component_register(
    SRCS "mongoose.c"
    INCLUDE_DIRS "."
    REQUIRES lwip app_update esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MG_ARCH=MG_ARCH_ESP32
    MG_ENABLE_LWIP=1
    MG_ENABLE_PACKED_FS=0
    MG_ENABLE_MBEDTLS=0
    MG_ENABLE_LINES=0
    MG_ENABLE_LOG=0
)
