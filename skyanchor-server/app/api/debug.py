from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from app.db.database import get_order
from app.services.order_status_clean import set_order_status

router = APIRouter(prefix="/api/debug", tags=["debug"])


class MockStateRequest(BaseModel):
    status: str = Field(..., pattern="^(pending_start|delivering|tag_matched|acting|delivered|failed|cancelled)$")
    note: str = Field(default="", max_length=200)
    matched_tag_id: int | None = Field(default=None, ge=1, le=999)
    last_device_state: str | None = Field(default=None, max_length=64)


@router.post("/orders/{order_id}/state")
def mock_order_state_endpoint(order_id: str, body: MockStateRequest) -> dict:
    order = get_order(order_id)
    if order is None:
        raise HTTPException(status_code=404, detail="Order not found")

    updated = set_order_status(
        order_id=order_id,
        status=body.status,
        note=body.note,
        matched_tag_id=body.matched_tag_id,
        last_device_state=body.last_device_state or body.status,
    )
    if updated is None:
        raise HTTPException(status_code=500, detail="Failed to update order")
    return updated
