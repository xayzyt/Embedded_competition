# SkyAnchor MiniApp

SkyAnchor 比赛体验版小程序，当前主链路为：

`用户手机 / 配送手机 -> 微信云函数 skyanchorService -> EMQX MQTT -> ESP32`

当前版本不再依赖本地 Python 后端或公网自建服务器，适合用微信小程序体验版做现场演示。

## Import

1. 打开微信开发者工具。
2. 导入目录 `skyanchor-miniapp`。
3. 确认 AppID 为 `wxe56e63bb28b1339e`。
4. 确认云环境为 `cloud1-d5g90ikff6eed3f26`。

## Current Routes

- `pages/role/index`
- `pages/sender-dispatch/index`
- `pages/user-orders/index`
- `pages/order-panel/index`

## Demo Profile

- 默认用户 ID：`receiver`
- 配送端手动选择 AprilTag：`0` 或 `1`
- MQTT 设备名：`skyanchor-p4`
- MQTT topic 前缀：`skyanchor`

如果旧手机缓存里还存着 `receiver_003` 或 `receiver__003`，小程序启动时会自动迁移成 `receiver`。其他手动输入过的用户 ID 会保留。

## Experience Release Flow

1. 在微信公众平台把演示用的两个微信号加入体验成员。
2. 在微信开发者工具中右键 `cloudfunctions/skyanchorService`。
3. 选择“上传并部署：云端安装依赖”。
4. 点击右上角“上传”，填写版本号，例如 `demo-20260425`。
5. 在版本管理里把该版本设为体验版。
6. 用户手机和配送手机分别扫码体验版二维码。

## Phone And Network Setup

现场可以使用三台手机：

- 手机 A：用户端，使用自己的移动数据。
- 手机 B：配送端，使用自己的移动数据。
- 手机 C：给 ESP32 开热点，热点需要能访问公网。

ESP32 固件当前 Wi-Fi 配置为：

- SSID：`5705`
- 密码：`12345678`

用户手机和配送手机不需要与 ESP32 在同一个局域网。只要三者都能访问公网，云函数和 EMQX MQTT 就能完成闭环。建议手机 C 开 2.4GHz 热点，并在比赛前确认串口日志出现：

- `wifi got ip`
- `EMQX mqtt connected`
- `system ready`

## Demo Flow

1. 用户端进入 `我是用户`，确认用户 ID 为 `receiver`。
2. 用户端点击 `我要下单` 并确认。
3. 配送端进入 `我是配送员`，刷新并打开待调度订单。
4. 配送端选择 `AprilTag 0` 或 `AprilTag 1`。
5. 配送端点击 `开始配送`。
6. ESP32 串口应看到 MQTT 收到 `start_task`。
7. 小程序订单状态随板端状态更新到配送、识别、执行和送达。

## Historical Local Backend Note

旧版曾经使用 `skyanchor-server` 本地后端和 `http://127.0.0.1:8000` 调试路径。当前体验版演示不要按旧本地后端流程发布；该路径只作为历史调试说明保留。
