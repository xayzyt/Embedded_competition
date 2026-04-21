# SkyAnchor MiniApp

Native WeChat mini program scaffold for:

- sender side: create orders and view sender orders
- receiver side: view notifications and open order details

## Import

1. Open WeChat DevTools.
2. Import the folder `skyanchor-miniapp`.
3. Use the generated test AppID or keep `touristappid` for local preview.

## Local backend

The app points to:

- `http://127.0.0.1:8000`

This is intended for the WeChat DevTools simulator.

For local development, enable:

- `Do not verify valid domain names, web-view, TLS version, and HTTPS certificates`

For real phone testing, replace the backend URL with a public HTTPS domain and configure it in the mini program admin console.

## Current routes

- `pages/role/index`
- `pages/sender-dispatch/index`
- `pages/user-orders/index`
- `pages/order-panel/index`

## Useful flow

1. Open the role page and choose `我是用户` or `我是配送员`.
2. In the user page, save `receiver_003`.
3. Click `我要下单`, then confirm the modal to create an order directly.
4. Switch to the dispatch page, open the pending order, and manually choose drone `0` or `1`.
5. Start delivery and watch the order detail page update automatically.

## Fixed demo profile

Use the same profile across the miniapp, backend, and board demos:

- receiver_id: `receiver_003`
- target_id: `3`
- dispatch devices: `0` and `1`

The miniapp now defaults to the receiver profile above, while the dispatch side manually chooses drone `0` or `1`.

## Recommended bring-up order

1. Start `skyanchor-server`
2. Open `http://127.0.0.1:8000/health`
3. Confirm `ok=true` and `mqtt_started=true`
4. Bring the board online
5. Open WeChat DevTools and start the sender/receiver demo flow

## Current integration notes

- When the backend is offline, the miniapp should say that `skyanchor-server` is not running.
- When the backend is online but MQTT is not ready, the miniapp should allow order creation but clearly block `开始配送`.
- `order-panel` polls active orders every 2 seconds.
- `user-orders` polls order lists every 3 seconds.

## Closed-loop checklist

Use the same demo profile across the whole project:

- `receiver_id = receiver_003`
- `target_id = 3`
- `dispatch devices = 0,1`

Recommended order of operations:

1. Start `skyanchor-server`.
2. Confirm `GET /health` returns `ok=true` and `mqtt_started=true`.
3. Bring the board online and wait for `wifi got ip`, `EMQX mqtt connected`, and `system ready`.
4. Open WeChat DevTools.
5. In the miniapp, choose `我是用户`, save `receiver_003`, and click `我要下单`.
6. Switch to `我是配送员`, open the pending order, choose drone `0` or `1`, and click `开始配送`.
7. Open the order detail page and watch the status move to the delivered state.
