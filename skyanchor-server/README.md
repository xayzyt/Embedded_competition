# SkyAnchor Server

This is the minimal backend that bridges:

- sender/receiver clients
- EMQX MQTT
- the current ESP32-P4 device state flow

## What it does

- create delivery orders
- publish `start_task` and `cancel` MQTT commands
- subscribe to device `ack` and `state`
- update order status in SQLite
- create receiver-facing notifications for `delivered`, `failed`, and `cancelled`

## Quick start

1. Copy `.env.example` to `.env` and fill in your EMQX credentials.
2. Create a virtual environment and install dependencies.

Using `uv`:

```powershell
uv venv
.venv\Scripts\Activate.ps1
uv pip install -e .
```

Using `pip`:

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install -e .
```

3. Run the server:

```powershell
uvicorn app.main:app --reload --host 127.0.0.1 --port 8000
```

4. Open:

- Swagger UI: `http://127.0.0.1:8000/docs`
- Health: `http://127.0.0.1:8000/health`

The current final demo profile uses two dispatchable drones:

- `MQTT_DEFAULT_DEVICE=0`
- `MQTT_DISPATCH_DEVICES=0,1`

## Current flow

1. `POST /api/orders`
2. `POST /api/orders/{order_id}/assign`
3. `POST /api/orders/{order_id}/start`
4. server publishes MQTT `start_task`
5. server consumes device `ack` and `state`
6. order becomes `pending_start`, `delivering`, `tag_matched`, `acting`, `delivered`, `failed`, or `cancelled`

## Useful endpoints

- `GET /api/orders`
- `GET /api/orders/{order_id}`
- `GET /api/notifications?user_id=receiver_xxx`
- `POST /api/notifications/{id}/read`

## Debug endpoint

To develop the miniapp without waiting for real hardware runs:

- `POST /api/debug/orders/{order_id}/state`

Example body:

```json
{
  "status": "delivered",
  "note": "mock delivered",
  "matched_tag_id": 3,
  "last_device_state": "completed"
}
```

## Notes

- The current device `state` payload does not include `order_id`, so this backend correlates active orders by `device_name + target_id`.
- `ack` correlation uses `request_id`, and this server sets `request_id = order_id`.

## Closed-loop checklist

Before starting the real sender -> backend -> board -> receiver demo:

1. Start `skyanchor-server`.
2. Confirm `GET /health` returns:
   - `ok=true`
   - `mqtt_started=true`
3. Make sure the board log already contains:
   - `wifi got ip`
   - `EMQX mqtt connected`
   - `system ready`

In this project, `mqtt_started=true` means the backend is already connected to the MQTT broker and can publish commands immediately.
