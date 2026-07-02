# 项目文件速查 & 答辩指南

> 只看你们自己写的代码，`managed_components/` 全是 ESP-IDF 官方库，不管。

---

## 一、每个文件一句话总结

### 入口

| 文件 | 一句话 |
|------|--------|
| [main/main.c](main/main.c) | **系统总装配**：初始化 NVS→屏幕→各模块→相机管线，协调主屏/预览切换、天气保护、异常演示 |

### 控制层 (components/control/) — 核心大脑

| 文件 | 一句话 |
|------|--------|
| [app_ctrl.h](components/control/app_ctrl.h) / [.c](components/control/app_ctrl.c) | **对接主状态机**：融合 AI 门控 + AprilTag 判定 + CH32 机械状态，每 60ms 跑一次控制循环，决定何时发对接命令 |
| [app_task.h](components/control/app_task.h) / [.c](components/control/app_task.c) | **任务生命周期管理**：维护目标 ID、任务状态（配置→等待→认证→对接→完成），通过事件回调通知 UI/云端 |
| [app_dock_judge.h](components/control/app_dock_judge.h) / [.c](components/control/app_dock_judge.c) | **AprilTag 判定器**：对视觉结果做 EMA 滤波 + 门控条件（居中/靠近/稳定/距离）+ 滞回状态机，输出 SEARCHING→READY_TO_DOCK |
| [app_ch32_link.h](components/control/app_ch32_link.h) / [.c](components/control/app_ch32_link.c) | **CH32 串口协议层**：UART 二进制帧编解码、ACK/NACK 匹配、ready 状态缓存，封装机械命令收发 |
| [app_cloud.h](components/control/app_cloud.h) / [.c](components/control/app_cloud.c) | **云端连接模块**：ESP-Hosted Wi-Fi → SNTP 对时 → MQTT 上线，上报任务状态、接收远程命令、查询天气 |

### 控制层辅助 (components/control/private/)

| 文件 | 一句话 |
|------|--------|
| [app_ctrl_proto.h](components/control/private/app_ctrl_proto.h) / [.c](components/control/private/app_ctrl_proto.c) | CH32 协议语义判断（是否 busy、是否等待货物窗口、错误分类） |
| [app_ctrl_text.h](components/control/private/app_ctrl_text.h) / [.c](components/control/private/app_ctrl_text.c) | 控制状态文案格式化，把内部数据转成 UI 可显示的字符串 |
| [app_cloud_cmd.h](components/control/private/app_cloud_cmd.h) / [.c](components/control/private/app_cloud_cmd.c) | MQTT JSON 命令解析（set_target / start / cancel） |
| [app_cloud_internal.h](components/control/private/app_cloud_internal.h) | 云端模块内部共享状态（Wi-Fi/MQTT/天气/事件组） |
| [app_cloud_messaging.c](components/control/private/app_cloud_messaging.c) | MQTT 消息收发与状态上报 |
| [app_cloud_weather.c](components/control/private/app_cloud_weather.c) | 天气查询任务（HTTP 请求 + 恶劣天气保护策略） |

### 视觉层 (components/vision_ui/) — 眼睛 + 屏幕

| 文件 | 一句话 |
|------|--------|
| [app_vision.h](components/vision_ui/app_vision.h) / [.c](components/vision_ui/app_vision.c) | **视觉管线**：接收 RGB565 帧→下采样灰度图→调 AprilTag 检测→发布结果 |
| [app_apriltag.h](components/vision_ui/app_apriltag.h) / [.c](components/vision_ui/app_apriltag.c) | **自研 AprilTag 检测器**：二值化→连通域→透视采样→与 tag36h11 码表匹配（纯 C 实现，不依赖第三方库） |
| [app_ui.h](components/vision_ui/app_ui.h) / [.c](components/vision_ui/app_ui.c) | **HUD 预览 UI**：相机画面 + 对接框 + 状态叠加文字 + 抓图按钮 |
| [app_ui_main.c](components/vision_ui/app_ui_main.c) | **主屏 UI**：任务阶段条、Wi-Fi/MQTT/CH32 状态灯、天气、时钟、异常演示按钮和语音开关 |
| [app_ai_capture.h](components/vision_ui/app_ai_capture.h) / [.c](components/vision_ui/app_ai_capture.c) | **AI 样本采集**：从相机帧抽样保存为 BMP 到 SD 卡（分有/无无人机两个目录） |
| [app_ui_assets.c](components/vision_ui/app_ui_assets.c) + assets/*.c | LVGL 字体和 logo 图片资源 |

### 相机层 (components/camera/) — 图像输入

| 文件 | 一句话 |
|------|--------|
| [app_camera.h](components/camera/app_camera.h) / [.c](components/camera/app_camera.c) | **相机预览**：V4L2 取帧 → PPA 硬件缩放 → LVGL canvas 显示 + 帧路由分发 |
| [app_video.h](components/camera/app_video.h) / [.c](components/camera/app_video.c) | **V4L2 设备封装**：打开设备、格式协商、缓冲管理、流任务 |
| [app_camera_route.h](components/camera/private/app_camera_route.h) / [.c](components/camera/private/app_camera_route.c) | **帧路由决策**：每帧决定送给 AI / 视觉 / 抓图 三个消费者的哪几个 |

### AI 层 (components/drone_ai/)

| 文件 | 一句话 |
|------|--------|
| [app_drone_ai.h](components/drone_ai/app_drone_ai.h) / [.cpp](components/drone_ai/app_drone_ai.cpp) | **无人机识别 AI**：ESP DL 模型推理，判断画面中是否有无人机（门控：确认有无人机后才启用 AprilTag） |

### 基础设施 (components/bsp/ + components/app_types/)

| 文件 | 一句话 |
|------|--------|
| [bsp_display_port.h](components/bsp/bsp_display_port.h) / [.c](components/bsp/bsp_display_port.c) | 显示 BSP 封装（LVGL 端口 + MIPI DSI 配置） |
| [app_image_utils.h](components/app_types/app_image_utils.h) / [.c](components/app_types/app_image_utils.c) | 图像通用工具（RGB565 转灰度、居中裁剪计算） |
| [app_dock_types.h](components/app_types/app_dock_types.h) | 对接状态枚举和结果结构体，跨模块共享 |

---

## 二、答辩可能被追问的地方

### 🔴 app_ctrl.c — 对接主状态机

> **"控制循环怎么工作的？"**

每 60ms 执行一次：
1. 读任务快照（目标 ID、任务状态）
2. 读视觉判定结果（对接状态、是否居中/靠近）
3. 检查 AI 无人机确认门控
4. 读 CH32 机械状态
5. 根据以上综合决定：发探测命令 / 发对接命令 / 等待 / 上报完成

> **追问：为什么用 60ms？**

视觉帧率约 30fps（~33ms/帧），60ms 保证不遗漏帧也不重复处理同一帧。

---

### 🔴 app_dock_judge.c — AprilTag 判定器

> **"EMA 滤波为什么选 shift=2？"**

shift=2 → α=0.25 → 新值 = 旧值×0.75 + 采样值×0.25。经验值，在"跟得紧"和"够平滑"之间折中。现场可调。

> **"滞回状态机是什么意思？"**

进状态需要连续 N 帧满足条件，出状态需要连续 M 帧不满足——防止单帧噪声导致状态反复横跳。比如进 READY_TO_DOCK 需要连续 1 帧 pass，退出需要连续 6 帧 bad。

> **"距离怎么算出来的？"**

小孔成像：`距离 = 焦距 × 标签实际尺寸 / 像素边长`。焦距 314px、标签 60mm，测得边长像素即可反算。

---

### 🔴 app_apriltag.c — 自研检测器

> **"为什么不用现成的 AprilTag 库？"**

自研版本裁剪了不需要的标签族（只保留 tag36h11），省 Flash/RAM，且可以嵌入自己的去噪和筛选逻辑。

> **追问：tag36h11 码表有多少个码？**

587 个（代码里 `s_tag36h11_codes[587]`），每个码用汉明距离匹配。

---

### 🔴 app_ch32_link.c — 串口协议

> **"CH32 是什么？协议怎么设计的？"**

CH32 是一颗国产 MCU（类似 STM32），负责机械控制（门、托盘、传感器）。ESP32 通过 UART 发二进制命令帧（帧头 0x55 0xAA + 类型 + 命令 + CRC），CH32 回复 ACK/NACK 或状态帧。

---

### 🔴 app_drone_ai.cpp — 无人机 AI

> **"为什么 AI 要放在 AprilTag 前面？"**

AI 做粗筛——先确认画面里真的有无人机，再启用 AprilTag 做精确定位。避免 AprilTag 在没有无人机时误识别环境中的类标签图案。

> **追问：模型多大？推理一帧多久？**

输入 128×128 RGB，用 ESP32-P4 的神经网络加速器推理，具体耗时看模型结构（是你们可以测一下的答辩数据）。

---

### 🔴 app_task.c — 任务状态管理

> **"为什么到处 taskENTER_CRITICAL / taskEXIT_CRITICAL？"**

`s_rt` 是全局状态，被多个 FreeRTOS 任务并发访问（控制任务、UI 刷新、MQTT 回调、CH32 中断）。spinlock 保护，但临界区极短（只做内存拷贝），耗时 I/O（NVS Flash）放锁外面。

---

### 🔴 main.c — 启动流程

> **"启动顺序为什么这么排？"**

```
NVS → 屏幕 → UI 启动页 → CH32 串口 → 视觉管线 → 对接判定器 
→ 任务模块 → AI 模型 → 控制循环 → 相机预览
```

原则：不依赖硬件的先启动（NVS/屏幕），依赖外部设备的后启动（相机），耗时的用后台任务异步（云端 Wi-Fi）。相机更重，只在任务进入视觉阶段时才启动。

---

### 🔴 架构追问

> **"两个芯片怎么分工？"**

| 芯片 | 职责 |
|------|------|
| ESP32-P4 | 主控：相机、AI 推理、AprilTag、UI、Wi-Fi/MQTT、任务调度 |
| CH32 MCU | 从控：机械执行（开门/伸托盘/检测货物/关门），通过 UART 接收命令 |

> **"系统实时性怎么保证？"**

- 控制循环 60ms 固定周期
- 临界区极短（微秒级）
- 耗时操作异步化（云端放后台任务、NVS 读写放锁外）
- FreeRTOS 优先级分配：相机流 > 控制 > UI > 云端
