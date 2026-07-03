idf_component_register(
    SRCS "app_delivery_photo.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_types
        esp32_p4_function_ev_board
        esp_new_jpeg
        heap
        mbedtls
)
