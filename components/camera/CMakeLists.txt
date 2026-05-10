idf_component_register(
    SRCS "app_camera.c" "app_video.c"
    INCLUDE_DIRS "."
    REQUIRES vision_ui lvgl esp32_p4_function_ev_board esp_video esp_driver_ppa
)
