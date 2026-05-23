from datetime import datetime
from uuid import uuid4

from fastapi import APIRouter, HTTPException, Query
from pydantic import BaseModel, Field

from app.config import get_settings
from app.db.database import (
    assign_order_dispatch,
    create_order,
    get_order,
    get_order_events,
    list_orders,
    mark_order_started,
)
from app.mqtt.client import mqtt_bridge
from app.services.order_status_clean import set_order_status

router = APIRouter(prefix="/api/orders", tags=["orders"])
UNASSIGNED_TARGET_ID = -1
VALID_TARGET_IDS = {0, 1}
WEATHER_BLOCKED_DETAIL = "恶劣天气，暂停下单和配送"


class CreateOrderRequest(BaseModel):
    sender_id: str | None = Field(default=None, min_length=1, max_length=64)
    receiver_id: str = Field(..., min_length=1, max_length=64)
    target_id: int | None = Field(default=None, ge=0, le=1)


class AssignOrderRequest(BaseModel):
    target_id: int | None = Field(default=None, ge=0, le=1)


class StartOrderResponse(BaseModel):
    order_id: str
    status: str
    mqtt_sent: bool


def build_order_id() -> str:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    suffix = uuid4().hex[:6].upper()
    return f"ORD-{stamp}-{suffix}"


def ensure_weather_allows_orders(device_name: str | None = None) -> None:
    settings = get_settings()
    target_device = str(device_name or settings.mqtt_default_device or "").strip()
    if not target_device:
        return
    if mqtt_bridge.is_ready and mqtt_bridge.latest_device_state(target_device) is None:
        raise HTTPException(status_code=409, detail="未收到板端状态，暂时不能下单和配送")
    if mqtt_bridge.is_weather_blocked(target_device):
        raise HTTPException(status_code=409, detail=WEATHER_BLOCKED_DETAIL)


@router.post("")
def create_order_endpoint(body: CreateOrderRequest) -> dict:
    ensure_weather_allows_orders()
    target_id = body.target_id if body.target_id is not None else UNASSIGNED_TARGET_ID

    order = create_order(
        order_id=build_order_id(),
        sender_id=body.sender_id or body.receiver_id,
        receiver_id=body.receiver_id,
        device_name="",
        target_id=target_id,
        note="",
    )
    return order


@router.get("")
def list_orders_endpoint(
    role: str | None = Query(default=None, pattern="^(sender|receiver)?$"),
    user_id: str | None = Query(default=None),
) -> list[dict]:
    return list_orders(role=role, user_id=user_id)


@router.get("/{order_id}")
def get_order_endpoint(order_id: str) -> dict:
    order = get_order(order_id)
    if order is None:
        raise HTTPException(status_code=404, detail="Order not found")

    return {
        "order": order,
        "events": get_order_events(order_id),
    }


@router.post("/{order_id}/assign")
def assign_order_endpoint(order_id: str, body: AssignOrderRequest) -> dict:
    order = get_order(order_id)
    if order is None:
        raise HTTPException(status_code=404, detail="Order not found")

    if order["status"] != "created":
        raise HTTPException(status_code=400, detail=f"Order cannot be assigned from status={order['status']}")

    if body.target_id is None:
        raise HTTPException(status_code=400, detail="target_id is required for manual assignment")

    settings = get_settings()
    device_name = str(settings.mqtt_default_device or "").strip()
    if not device_name:
        raise HTTPException(status_code=500, detail="Default MQTT device is not configured")

    updated = assign_order_dispatch(order_id, device_name, body.target_id)
    return updated or {"order_id": order_id, "device_name": device_name, "target_id": body.target_id}


@router.post("/{order_id}/start", response_model=StartOrderResponse)
def start_order_endpoint(order_id: str) -> StartOrderResponse:
    order = get_order(order_id)
    if order is None:
        raise HTTPException(status_code=404, detail="Order not found")

    if order["status"] not in {"created", "pending_start"}:
        raise HTTPException(status_code=400, detail=f"Order cannot be started from status={order['status']}")

    if not str(order["device_name"]).strip():
        raise HTTPException(status_code=400, detail="Order has no assigned device")
    if order["target_id"] not in VALID_TARGET_IDS:
        raise HTTPException(status_code=400, detail="Order has no assigned AprilTag")

    ensure_weather_allows_orders(order["device_name"])

    try:
        mqtt_bridge.publish_command(
            order["device_name"],
            {
                "cmd": "start_task",
                "order_id": order["order_id"],
                "target_id": order["target_id"],
                "request_id": order["request_id"],
            },
        )
    except RuntimeError as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc

    mark_order_started(order_id, note="mqtt start_task sent")
    updated = get_order(order_id)
    return StartOrderResponse(order_id=order_id, status=updated["status"], mqtt_sent=True)


@router.post("/{order_id}/cancel")
def cancel_order_endpoint(order_id: str) -> dict:
    order = get_order(order_id)
    if order is None:
        raise HTTPException(status_code=404, detail="Order not found")

    if order["status"] == "created":
        updated = set_order_status(order_id, "cancelled", note="cancel requested by dispatch")
        return updated or {"order_id": order_id, "status": "cancelled"}

    try:
        mqtt_bridge.publish_command(
            order["device_name"],
            {
                "cmd": "cancel",
                "target_id": order["target_id"],
                "request_id": order["request_id"],
            },
        )
    except RuntimeError as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc

    updated = set_order_status(order_id, "cancelled", note="cancel requested by backend")
    return updated or {"order_id": order_id, "status": "cancelled"}
