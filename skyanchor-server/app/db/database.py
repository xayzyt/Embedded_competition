import json
import sqlite3
import threading
from datetime import UTC, datetime
from typing import Any

from app.config import get_settings

DB_LOCK = threading.Lock()
ACTIVE_ORDER_STATUSES = ("created", "pending_start", "delivering", "tag_matched", "acting")
CLEARABLE_ORDER_STATUSES = ("created", "delivered", "failed", "cancelled")


def utc_now_text() -> str:
    return datetime.now(UTC).isoformat(timespec="seconds").replace("+00:00", "Z")


def get_connection() -> sqlite3.Connection:
    db_file = get_settings().database_file
    db_file.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(db_file, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn


def row_to_dict(row: sqlite3.Row | None) -> dict[str, Any] | None:
    return dict(row) if row is not None else None


def init_db() -> None:
    with DB_LOCK:
        with get_connection() as conn:
            conn.executescript(
                """
                CREATE TABLE IF NOT EXISTS orders (
                    order_id TEXT PRIMARY KEY,
                    sender_id TEXT NOT NULL,
                    receiver_id TEXT NOT NULL,
                    device_name TEXT NOT NULL,
                    target_id INTEGER NOT NULL,
                    request_id TEXT NOT NULL,
                    status TEXT NOT NULL,
                    note TEXT NOT NULL DEFAULT '',
                    matched_tag_id INTEGER NOT NULL DEFAULT 0,
                    last_device_state TEXT NOT NULL DEFAULT '',
                    created_at TEXT NOT NULL,
                    started_at TEXT,
                    delivered_at TEXT,
                    failed_at TEXT,
                    updated_at TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS order_events (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    order_id TEXT NOT NULL,
                    event_type TEXT NOT NULL,
                    event_data TEXT NOT NULL DEFAULT '{}',
                    created_at TEXT NOT NULL,
                    FOREIGN KEY(order_id) REFERENCES orders(order_id)
                );

                CREATE TABLE IF NOT EXISTS notifications (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    user_id TEXT NOT NULL,
                    order_id TEXT NOT NULL,
                    type TEXT NOT NULL,
                    title TEXT NOT NULL,
                    content TEXT NOT NULL,
                    read_flag INTEGER NOT NULL DEFAULT 0,
                    created_at TEXT NOT NULL,
                    read_at TEXT,
                    FOREIGN KEY(order_id) REFERENCES orders(order_id)
                );
                """
            )
            conn.commit()


def add_order_event(order_id: str, event_type: str, event_data: dict[str, Any] | None = None) -> None:
    now = utc_now_text()
    payload = json.dumps(event_data or {}, ensure_ascii=True)
    with DB_LOCK:
        with get_connection() as conn:
            conn.execute(
                """
                INSERT INTO order_events (order_id, event_type, event_data, created_at)
                VALUES (?, ?, ?, ?)
                """,
                (order_id, event_type, payload, now),
            )
            conn.commit()


def create_order(
    order_id: str,
    sender_id: str,
    receiver_id: str,
    device_name: str,
    target_id: int,
    note: str = "",
) -> dict[str, Any]:
    now = utc_now_text()
    with DB_LOCK:
        with get_connection() as conn:
            conn.execute(
                """
                INSERT INTO orders (
                    order_id, sender_id, receiver_id, device_name, target_id,
                    request_id, status, note, created_at, updated_at
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    order_id,
                    sender_id,
                    receiver_id,
                    device_name,
                    target_id,
                    order_id,
                    "created",
                    note,
                    now,
                    now,
                ),
            )
            conn.commit()

    add_order_event(
        order_id,
        "created",
        {
            "target_id": target_id,
            "device_name": device_name,
        },
    )
    return get_order(order_id)  # type: ignore[return-value]


def get_order(order_id: str) -> dict[str, Any] | None:
    with DB_LOCK:
        with get_connection() as conn:
            row = conn.execute("SELECT * FROM orders WHERE order_id = ?", (order_id,)).fetchone()
    return row_to_dict(row)


def list_orders(role: str | None = None, user_id: str | None = None) -> list[dict[str, Any]]:
    sql = "SELECT * FROM orders"
    params: list[Any] = []

    if role == "sender" and user_id:
        sql += " WHERE sender_id = ?"
        params.append(user_id)
    elif role == "receiver" and user_id:
        sql += " WHERE receiver_id = ?"
        params.append(user_id)

    sql += " ORDER BY created_at DESC"

    with DB_LOCK:
        with get_connection() as conn:
            rows = conn.execute(sql, params).fetchall()
    return [dict(row) for row in rows]


def delete_order_record(order_id: str) -> dict[str, Any] | None:
    with DB_LOCK:
        with get_connection() as conn:
            current = conn.execute("SELECT * FROM orders WHERE order_id = ?", (order_id,)).fetchone()
            if current is None:
                return None

            order = dict(current)
            conn.execute("DELETE FROM notifications WHERE order_id = ?", (order_id,))
            conn.execute("DELETE FROM order_events WHERE order_id = ?", (order_id,))
            conn.execute("DELETE FROM orders WHERE order_id = ?", (order_id,))
            conn.commit()

    return order


def get_order_events(order_id: str) -> list[dict[str, Any]]:
    with DB_LOCK:
        with get_connection() as conn:
            rows = conn.execute(
                "SELECT id, event_type, event_data, created_at FROM order_events WHERE order_id = ? ORDER BY id ASC",
                (order_id,),
            ).fetchall()
    return [dict(row) for row in rows]


def create_notification(
    user_id: str,
    order_id: str,
    type_: str,
    title: str,
    content: str,
) -> dict[str, Any]:
    now = utc_now_text()
    with DB_LOCK:
        with get_connection() as conn:
            existing = conn.execute(
                """
                SELECT * FROM notifications
                WHERE user_id = ? AND order_id = ? AND type = ?
                ORDER BY id DESC
                LIMIT 1
                """,
                (user_id, order_id, type_),
            ).fetchone()
            if existing is not None:
                return dict(existing)

            cursor = conn.execute(
                """
                INSERT INTO notifications (user_id, order_id, type, title, content, read_flag, created_at)
                VALUES (?, ?, ?, ?, ?, 0, ?)
                """,
                (user_id, order_id, type_, title, content, now),
            )
            notification_id = cursor.lastrowid
            conn.commit()

            row = conn.execute("SELECT * FROM notifications WHERE id = ?", (notification_id,)).fetchone()
    return dict(row)  # type: ignore[arg-type]


def list_notifications(user_id: str, unread_only: bool = False) -> list[dict[str, Any]]:
    sql = """
        SELECT id, user_id, order_id, type, title, content, read_flag, created_at, read_at
        FROM notifications
        WHERE user_id = ?
    """
    params: list[Any] = [user_id]

    if unread_only:
        sql += " AND read_flag = 0"

    sql += " ORDER BY created_at DESC, id DESC"

    with DB_LOCK:
        with get_connection() as conn:
            rows = conn.execute(sql, params).fetchall()
    return [dict(row) for row in rows]


def mark_notification_read(notification_id: int) -> dict[str, Any] | None:
    now = utc_now_text()
    with DB_LOCK:
        with get_connection() as conn:
            current = conn.execute("SELECT * FROM notifications WHERE id = ?", (notification_id,)).fetchone()
            if current is None:
                return None

            conn.execute(
                """
                UPDATE notifications
                SET read_flag = 1, read_at = COALESCE(read_at, ?)
                WHERE id = ?
                """,
                (now, notification_id),
            )
            conn.commit()
            row = conn.execute("SELECT * FROM notifications WHERE id = ?", (notification_id,)).fetchone()
    return dict(row)  # type: ignore[arg-type]


def mark_order_started(order_id: str, note: str = "") -> dict[str, Any] | None:
    now = utc_now_text()
    with DB_LOCK:
        with get_connection() as conn:
            conn.execute(
                """
                UPDATE orders
                SET status = ?, note = ?, started_at = COALESCE(started_at, ?), updated_at = ?
                WHERE order_id = ?
                """,
                ("pending_start", note, now, now, order_id),
            )
            conn.commit()

    add_order_event(order_id, "start_requested", {"note": note})
    return get_order(order_id)


def assign_order_dispatch(order_id: str, device_name: str, target_id: int) -> dict[str, Any] | None:
    now = utc_now_text()
    with DB_LOCK:
        with get_connection() as conn:
            current = conn.execute("SELECT * FROM orders WHERE order_id = ?", (order_id,)).fetchone()
            if current is None:
                return None

            conn.execute(
                """
                UPDATE orders
                SET device_name = ?, target_id = ?, updated_at = ?
                WHERE order_id = ?
                """,
                (device_name, target_id, now, order_id),
            )
            conn.commit()

    add_order_event(
        order_id,
        "dispatch_assigned",
        {
            "device_name": device_name,
            "target_id": target_id,
        },
    )
    return get_order(order_id)


def update_order_status(
    order_id: str,
    status: str,
    note: str = "",
    matched_tag_id: int | None = None,
    last_device_state: str | None = None,
) -> dict[str, Any] | None:
    now = utc_now_text()
    delivered_at = now if status == "delivered" else None
    failed_at = now if status == "failed" else None

    with DB_LOCK:
        with get_connection() as conn:
            current = conn.execute("SELECT * FROM orders WHERE order_id = ?", (order_id,)).fetchone()
            if current is None:
                return None

            conn.execute(
                """
                UPDATE orders
                SET status = ?,
                    note = ?,
                    matched_tag_id = ?,
                    last_device_state = ?,
                    delivered_at = COALESCE(?, delivered_at),
                    failed_at = COALESCE(?, failed_at),
                    updated_at = ?
                WHERE order_id = ?
                """,
                (
                    status,
                    note or current["note"],
                    matched_tag_id if matched_tag_id is not None else current["matched_tag_id"],
                    last_device_state if last_device_state is not None else current["last_device_state"],
                    delivered_at,
                    failed_at,
                    now,
                    order_id,
                ),
            )
            conn.commit()

    add_order_event(
        order_id,
        "status_changed",
        {
            "status": status,
            "note": note,
            "matched_tag_id": matched_tag_id,
            "last_device_state": last_device_state,
        },
    )
    return get_order(order_id)


def find_active_order(device_name: str, target_id: int) -> dict[str, Any] | None:
    placeholders = ",".join("?" for _ in ACTIVE_ORDER_STATUSES)
    sql = f"""
        SELECT * FROM orders
        WHERE device_name = ? AND target_id = ? AND status IN ({placeholders})
        ORDER BY created_at DESC
        LIMIT 1
    """
    params: list[Any] = [device_name, target_id, *ACTIVE_ORDER_STATUSES]

    with DB_LOCK:
        with get_connection() as conn:
            row = conn.execute(sql, params).fetchone()
    return row_to_dict(row)
