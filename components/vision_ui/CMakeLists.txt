idf_component_register(
    SRCS "app_vision.c" "app_apriltag.c" "app_ai_capture.c" "app_ui.c" "assets/font_loading_cn.c" "assets/font_main_title_cn.c" "assets/font_title_en.c"
    INCLUDE_DIRS "."
    REQUIRES app_types lvgl esp32_p4_function_ev_board esp_timer heap esp_lvgl_port
)

lvgl_port_create_c_image("assets/logo.png" "assets/" "ARGB8888" "NONE")

set(WEATHER_ICON_CODES
    0 1 2 3 4 5 6 7 8 9
    10 11 12 13 14 15 16 17 18 19
    20 21 22 23 24 26 27 28 29
    30 31 32 33 34 35 36 37 38 99
)
set(WEATHER_ICON_INPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/weather_icon_input")
set(WEATHER_ICON_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/weather_icon_output")
file(MAKE_DIRECTORY "${WEATHER_ICON_INPUT_DIR}" "${WEATHER_ICON_OUTPUT_DIR}")
foreach(code ${WEATHER_ICON_CODES})
    set(src_icon "${CMAKE_CURRENT_LIST_DIR}/assets/img/${code}@1x.png")
    set(dst_icon "${WEATHER_ICON_INPUT_DIR}/weather_${code}.png")
    if(EXISTS "${src_icon}")
        configure_file("${src_icon}" "${dst_icon}" COPYONLY)
    endif()
endforeach()
lvgl_port_create_c_image("${WEATHER_ICON_INPUT_DIR}" "${WEATHER_ICON_OUTPUT_DIR}/" "ARGB8888" "NONE")

lvgl_port_add_images(${COMPONENT_LIB} "assets/")
lvgl_port_add_images(${COMPONENT_LIB} "${WEATHER_ICON_OUTPUT_DIR}/")
