idf_component_register(
    SRCS "app_vision.c" "app_apriltag.c" "app_ai_capture.c" "app_ui.c"
    INCLUDE_DIRS "."
    REQUIRES app_types lvgl esp32_p4_function_ev_board esp_timer
)
