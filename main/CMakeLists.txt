idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES app_types camera vision_ui control drone_ai bsp nvs_flash
)
