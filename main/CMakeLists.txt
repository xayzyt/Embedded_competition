idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES app_types camera vision_ui control bsp nvs_flash
)
