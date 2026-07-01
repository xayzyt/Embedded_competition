idf_component_register(
    SRCS "app_audio_prompt.c"
    INCLUDE_DIRS "."
    EMBED_FILES
        "assets/ready.pcm"
        "assets/docking_complete.pcm"
        "assets/outer_door_opening.pcm"
        "assets/weather_paused.pcm"
        "assets/tray_retracting.pcm"
        "assets/apriltag_located.pcm"
        "assets/drone_identified.pcm"
    REQUIRES esp32_p4_function_ev_board heap
)
