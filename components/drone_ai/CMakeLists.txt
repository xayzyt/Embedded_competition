idf_component_register(
    SRCS "app_drone_ai.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_types vision_ui esp_timer esp-dl
)

set(DRONE_AI_MODEL_FILE "${CMAKE_SOURCE_DIR}/ai_models/drone_cls_pretrained_v3/drone_cls_p4_int8.espdl")
if(EXISTS "${DRONE_AI_MODEL_FILE}")
    esptool_py_flash_to_partition(flash "model" "${DRONE_AI_MODEL_FILE}")
else()
    message(WARNING
        "Drone AI model not found: ${DRONE_AI_MODEL_FILE}. "
        "Firmware can still be built, but drone AI requires flashing this .espdl file to the model partition.")
endif()
