# Embedded Competition Project

这是一个面向嵌入式竞赛的综合项目，围绕 ESP32-P4、MQTT、AprilTag 识别、任务状态机、小程序端和云端服务构建完整闭环。

仓库保持私有，主要用于项目复盘、面试展示和后续维护。

## 项目亮点

- ESP32-P4 固件与 FreeRTOS 任务组织
- Wi-Fi / MQTT 云端通信
- AprilTag 目标识别与任务触发
- 小程序用户端与调度端流程
- 云函数 / 后端服务与设备状态同步
- 从下单、调度、开始任务到设备状态回传的闭环演示

## 系统架构

```text
小程序用户端 / 调度端
        |
        v
云函数或本地后端
        |
        v
MQTT Broker
        |
        v
ESP32-P4 固件
        |
        v
状态回传与订单更新
```

## 仓库结构

```text
main/                 ESP32-P4 固件源码
skyanchor-miniapp/    微信小程序与云函数
skyanchor-server/     本地 FastAPI 调试后端
DEMO_RUNBOOK.md       演示流程和排查清单
```

## 固件模块

- `app_cloud.c`: Wi-Fi、MQTT、命令接收、ACK 和状态上报
- `app_task.c`: 任务状态机和远程任务请求处理
- `app_apriltag.c`: AprilTag 识别相关逻辑
- `app_ctrl.c`: 控制层抽象
- `app_ch32_link.c`: 与下位控制器的通信
- `app_ui.c`: 显示和交互反馈

## 面试展示建议

这个项目适合重点展示：

- 嵌入式系统分层设计
- MQTT 协议在设备协同中的使用
- 任务状态机设计
- 端、云、设备闭环联调能力
- 比赛项目从需求到演示版本的工程落地能力

## 说明

仓库中不应公开真实 MQTT 密码、Wi-Fi 密码或私有云配置。需要演示时建议使用截图、视频和架构图辅助说明。
