# SkyAnchor Cloud Functions

当前小程序只部署一个聚合云函数：

- `skyanchorService`
  - 提供健康检查、建单、订单列表、订单详情、分配 AprilTag、开始配送、手动回收托盘、取消订单。
  - 使用微信云开发数据库保存 `orders` 与 `order_events`。
  - 通过 EMQX MQTT 向 ESP32-P4 下发 `start_task` / `manual_retract` / `cancel`，并读取 retained state 更新订单状态。

演示前请在微信开发者工具中右键 `cloudfunctions/skyanchorService`，选择“上传并部署：云端安装依赖”。
