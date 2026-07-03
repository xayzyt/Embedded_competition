#pragma once

#include <stdbool.h>

// 相机帧分发策略。
//
// 相机回调每收到一帧都会调用 app_camera_route_select()。该模块只决定
// 当前帧应该送往哪些消费者，不持有图像缓存，也不执行推理或文件写入。
// 三个消费者可以同时命中，因此调用方必须分别检查每个 due 标志。

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ai_due;      // 本帧是否提交无人机分类模型。
    bool vision_due;  // 本帧是否提交 AprilTag 视觉管线。
    bool capture_due; // 本帧是否保存为训练样本。
    bool delivery_due; // 本帧是否保存为送达照片。
} app_camera_frame_route_t;

// 清空抽样计数和门控边沿状态；重新启动预览前调用。
void app_camera_route_reset(void);

// 根据任务状态、AI 确认状态和抓图状态生成当前帧的分发决策。
app_camera_frame_route_t app_camera_route_select(void);

#ifdef __cplusplus
}
#endif
