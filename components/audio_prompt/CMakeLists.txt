idf_component_register(
    SRCS "app_audio_prompt.c" "app_audio_prompt_hw.c"
    INCLUDE_DIRS "."
    EMBED_FILES
        "assets/ready.pcm"
        "assets/docking_complete.pcm"
        "assets/outer_door_opening.pcm"
        "assets/weather_paused.pcm"
        "assets/tray_retracting.pcm"
        "assets/apriltag_located.pcm"
        "assets/drone_identified.pcm"
        "assets/drone_not_detected.pcm"
        "assets/drone_returned.pcm"
    REQUIRES esp32_p4_function_ev_board esp_driver_i2s heap nvs_flash
)
