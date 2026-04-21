from typing import Any

from app.db.database import create_notification, get_order, update_order_status


TERMINAL_NOTIFICATION_META = {
    "delivered": ("order_delivered", "订单已送达", "订单已经送达"),
    "failed": ("order_failed", "订单配送失败", "投递失败"),
    "cancelled": ("order_cancelled", "订单已取消", "订单已取消"),
}


def set_order_status(
    order_id: str,
    status: str,
    note: str = "",
    matched_tag_id: int | None = None,
    last_device_state: str | None = None,
) -> dict[str, Any] | None:
    previous = get_order(order_id)
    if previous is None:
        return None

    updated = update_order_status(
        order_id=order_id,
        status=status,
        note=note,
        matched_tag_id=matched_tag_id,
        last_device_state=last_device_state,
    )
    if updated is None or previous["status"] == status:
        return updated

    if status in TERMINAL_NOTIFICATION_META:
        type_, title, fallback = TERMINAL_NOTIFICATION_META[status]
        create_notification(
            user_id=updated["receiver_id"],
            order_id=updated["order_id"],
            type_=type_,
            title=title,
            content=note or fallback,
        )

    return updated
