idf_component_register(
    SRCS "app_camera.c" "private/app_camera_route.c" "app_video.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "private"
    REQUIRES vision_ui control delivery_photo drone_ai lvgl esp32_p4_function_ev_board esp_video esp_driver_ppa
)
