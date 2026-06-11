idf_component_register(
    SRCS "app_main_services.c" "app_preview_controller.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES app_types camera vision_ui control drone_ai bsp nvs_flash
)
