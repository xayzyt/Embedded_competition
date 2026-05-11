idf_component_register(
    SRCS "app_drone_ai.cpp"
    INCLUDE_DIRS "."
    REQUIRES vision_ui esp_timer esp-dl
)

set(DRONE_AI_MODEL_FILE "${CMAKE_SOURCE_DIR}/ai_models/drone_cls_pretrained_v3/drone_cls_p4_int8.espdl")
esptool_py_flash_to_partition(flash "model" "${DRONE_AI_MODEL_FILE}")
