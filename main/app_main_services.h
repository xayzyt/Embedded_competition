#pragma once

// 主屏按钮和后台服务接口。
#ifdef __cplusplus
extern "C" {
#endif

// 绑定取货、天气模拟和紧急保护按钮回调。
void app_main_services_bind_ui_callbacks(void);

// 启动云端初始化和连接状态刷新任务，可重复调用。
void app_main_services_start(void);

#ifdef __cplusplus
}
#endif
