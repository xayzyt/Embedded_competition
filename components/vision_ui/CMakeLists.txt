idf_component_register(
    SRCS "app_vision.c" "app_apriltag.c" "app_ai_capture.c" "app_ui.c" "font_loading_cn.c" "font_title_en.c"
    INCLUDE_DIRS "."
    REQUIRES app_types lvgl esp32_p4_function_ev_board esp_timer heap esp_lvgl_port
)

lvgl_port_create_c_image("images/logo.png" "images/" "ARGB8888" "NONE")
lvgl_port_add_images(${COMPONENT_LIB} "images/")
