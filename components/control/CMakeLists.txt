idf_component_register(
    SRCS "app_ctrl.c" "app_dock_judge.c" "app_task.c" "app_cloud.c" "app_ch32_link.c"
    INCLUDE_DIRS "."
    REQUIRES app_types vision_ui drone_ai nvs_flash esp_event esp_netif esp_wifi mqtt mbedtls heap json esp_driver_gpio esp_driver_uart
)
