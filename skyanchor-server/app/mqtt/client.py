import json
import ssl
import threading
from typing import Any

import paho.mqtt.client as mqtt

from app.config import get_settings
from app.db.database import (
    add_order_event,
    find_active_order,
    get_order,
)
from app.services.order_status_clean import set_order_status


class MQTTBridge:
    def __init__(self) -> None:
        self._settings = get_settings()
        self._client: mqtt.Client | None = None
        self._started = False
        self._connected = False
        self._lock = threading.Lock()
        self._device_states: dict[str, dict[str, Any]] = {}

    @property
    def is_started(self) -> bool:
        return self._started

    @property
    def is_ready(self) -> bool:
        return self._started and self._connected

    def start(self) -> None:
        if not self._settings.mqtt_host:
            return

        with self._lock:
            if self._started:
                return

            client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=self._settings.mqtt_client_id)
            if self._settings.mqtt_username:
                client.username_pw_set(self._settings.mqtt_username, self._settings.mqtt_password)

            if self._settings.mqtt_use_tls:
                client.tls_set(cert_reqs=ssl.CERT_REQUIRED)

            client.on_connect = self._on_connect
            client.on_message = self._on_message
            client.on_disconnect = self._on_disconnect

            self._client = client
            self._started = True
            self._connected = False

            try:
                client.connect(
                    host=self._settings.mqtt_host,
                    port=self._settings.mqtt_port,
                    keepalive=self._settings.mqtt_keepalive,
                )
                client.loop_start()
            except Exception:
                self._client = None
                self._started = False
                self._connected = False
                raise

    def stop(self) -> None:
        with self._lock:
            if self._client is not None:
                self._client.loop_stop()
                self._client.disconnect()
                self._client = None
            self._started = False
            self._connected = False

    def publish_command(self, device_name: str, payload: dict[str, Any]) -> None:
        if self._client is None or not self.is_ready:
            raise RuntimeError("MQTT broker is not connected")

        topic = self._settings.topic_cmd(device_name)
        body = json.dumps(payload, ensure_ascii=True)
        result = self._client.publish(topic, body, qos=1)
        result.wait_for_publish()
        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(f"MQTT publish failed: rc={result.rc}")

    def latest_device_state(self, device_name: str) -> dict[str, Any] | None:
        with self._lock:
            state = self._device_states.get(str(device_name or "").strip())
            return dict(state) if state is not None else None

    def is_weather_blocked(self, device_name: str) -> bool:
        state = self.latest_device_state(device_name)
        if not state:
            return False

        weather_mode = str(state.get("weather_mode", "")).strip()
        return (
            int(state.get("weather_blocked", 0) or 0) == 1
            or int(state.get("accept_orders", 1) or 0) == 0
            or weather_mode in {"cloud_guard", "emergency"}
        )

    def _on_connect(
        self,
        client: mqtt.Client,
        _userdata: Any,
        _connect_flags: dict[str, Any],
        reason_code: mqtt.ReasonCode,
        _properties: mqtt.Properties | None,
    ) -> None:
        if getattr(reason_code, "is_failure", False):
            self._connected = False
            return

        code_value = getattr(reason_code, "value", 0)
        if code_value not in (0, "0", None):
            self._connected = False
            return

        self._connected = True
        ack_topic = f"{self._settings.mqtt_topic_prefix}/+/ack"
        state_topic = f"{self._settings.mqtt_topic_prefix}/+/state"
        client.subscribe(ack_topic, qos=1)
        client.subscribe(state_topic, qos=1)

    def _on_disconnect(
        self,
        _client: mqtt.Client,
        _userdata: Any,
        _disconnect_flags: mqtt.DisconnectFlags,
        _reason_code: mqtt.ReasonCode,
        _properties: mqtt.Properties | None,
    ) -> None:
        self._connected = False
        return

    def _on_message(
        self,
        _client: mqtt.Client,
        _userdata: Any,
        message: mqtt.MQTTMessage,
    ) -> None:
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except Exception:
            return

        topic = message.topic
        device_name = self._parse_device_name(topic)
        if device_name is None:
            return

        if topic.endswith("/ack"):
            self._handle_ack(device_name, payload)
            return

        if topic.endswith("/state"):
            self._handle_state(device_name, payload)

    def _parse_device_name(self, topic: str) -> str | None:
        parts = topic.split("/")
        if len(parts) < 3 or parts[0] != self._settings.mqtt_topic_prefix:
            return None
        return parts[1]

    def _handle_ack(self, device_name: str, payload: dict[str, Any]) -> None:
        request_id = str(payload.get("request_id", "")).strip()
        if not request_id:
            return

        order = get_order(request_id)
        if order is None:
            return

        add_order_event(order["order_id"], "mqtt_ack", {"device_name": device_name, "payload": payload})

        code = int(payload.get("code", -1))
        cmd = str(payload.get("cmd", ""))
        msg = str(payload.get("msg", ""))
        if cmd == "start_task":
            if code == 0:
                if order["status"] in {"created", "pending_start"}:
                    set_order_status(
                        order["order_id"],
                        "pending_start",
                        note=msg or "mqtt accepted",
                    )
            else:
                set_order_status(
                    order["order_id"],
                    "failed",
                    note=msg or "start_failed",
                )

        if cmd == "cancel" and code == 0:
            set_order_status(order["order_id"], "cancelled", note=msg or "cancelled")

    def _handle_state(self, device_name: str, payload: dict[str, Any]) -> None:
        with self._lock:
            self._device_states[device_name] = dict(payload)

        order_id = str(payload.get("order_id", "")).strip()
        raw_target_id = payload.get("target_id", None)
        try:
            target_id = None if raw_target_id in (None, "") else int(raw_target_id)
        except (TypeError, ValueError):
            target_id = None

        order = get_order(order_id) if order_id else None
        if order is None and target_id is not None:
            order = find_active_order(device_name, target_id)
        if order is None:
            return

        add_order_event(order["order_id"], "device_state", {"device_name": device_name, "payload": payload})

        state = str(payload.get("state", "")).strip()
        note = str(payload.get("note", "")).strip()
        raw_matched_tag_id = payload.get("matched_tag_id", None)
        try:
            matched_tag_id = None if raw_matched_tag_id in (None, "") else int(raw_matched_tag_id)
        except (TypeError, ValueError):
            matched_tag_id = None
        fault = int(payload.get("fault", 0) or 0)
        cargo_received = int(payload.get("cargo_received", 0) or 0)

        next_status = self._map_device_state(state=state, fault=fault, cargo_received=cargo_received)
        if next_status is None:
            return

        set_order_status(
            order["order_id"],
            next_status,
            note=note or state,
            matched_tag_id=matched_tag_id,
            last_device_state=state,
        )

    @staticmethod
    def _map_device_state(state: str, fault: int, cargo_received: int) -> str | None:
        if cargo_received == 1 or state == "completed":
            return "delivered"
        if fault == 1 or state == "fault":
            return "failed"

        mapping = {
            "configured": "pending_start",
            "wait_approach": "delivering",
            "auth_passed": "tag_matched",
            "docking": "acting",
            "cancelled": "cancelled",
        }
        return mapping.get(state)


mqtt_bridge = MQTTBridge()
