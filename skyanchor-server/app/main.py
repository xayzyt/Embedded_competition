from contextlib import asynccontextmanager

from fastapi import FastAPI

from app.api.debug import router as debug_router
from app.api.notifications import router as notifications_router
from app.api.orders import router as orders_router
from app.config import get_settings
from app.db.database import init_db
from app.mqtt.client import mqtt_bridge

settings = get_settings()


@asynccontextmanager
async def lifespan(_app: FastAPI):
    init_db()
    mqtt_bridge.start()
    yield
    mqtt_bridge.stop()


app = FastAPI(title=settings.app_name, lifespan=lifespan)
app.include_router(orders_router)
app.include_router(notifications_router)
app.include_router(debug_router)


@app.get("/health", tags=["system"])
def health() -> dict:
    return {
        "ok": True,
        "app": settings.app_name,
        "mqtt_started": mqtt_bridge.is_ready,
        "default_device": settings.mqtt_default_device,
        "device_candidates": settings.mqtt_device_candidates,
    }
