from fastapi import APIRouter, HTTPException, Query

from app.db.database import list_notifications, mark_notification_read

router = APIRouter(prefix="/api/notifications", tags=["notifications"])


@router.get("")
def list_notifications_endpoint(
    user_id: str = Query(..., min_length=1),
    unread_only: bool = Query(default=False),
) -> list[dict]:
    return list_notifications(user_id=user_id, unread_only=unread_only)


@router.post("/{notification_id}/read")
def mark_notification_read_endpoint(notification_id: int) -> dict:
    notification = mark_notification_read(notification_id)
    if notification is None:
        raise HTTPException(status_code=404, detail="Notification not found")
    return notification
