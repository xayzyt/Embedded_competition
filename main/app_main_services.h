#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Bind main-screen actions before the screen becomes interactive.
void app_main_services_bind_ui_callbacks(void);

// Start cloud initialization and the connection-status refresh task.
void app_main_services_start(void);

#ifdef __cplusplus
}
#endif
