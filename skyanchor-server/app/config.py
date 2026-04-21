from functools import lru_cache
from pathlib import Path

from pydantic import computed_field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    app_name: str = "SkyAnchor Server"
    app_host: str = "127.0.0.1"
    app_port: int = 8000
    database_path: str = "./skyanchor.db"

    mqtt_host: str = ""
    mqtt_port: int = 8883
    mqtt_username: str = ""
    mqtt_password: str = ""
    mqtt_use_tls: bool = True
    mqtt_client_id: str = "skyanchor-server"
    mqtt_keepalive: int = 60
    mqtt_topic_prefix: str = "skyanchor"
    mqtt_default_device: str = "skyanchor-p4"
    mqtt_dispatch_devices: str = "skyanchor-p4"

    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        case_sensitive=False,
        extra="ignore",
    )

    @computed_field  # type: ignore[misc]
    @property
    def database_file(self) -> Path:
        base_dir = Path(__file__).resolve().parents[1]
        db_path = Path(self.database_path)
        return db_path if db_path.is_absolute() else (base_dir / db_path).resolve()

    @computed_field  # type: ignore[misc]
    @property
    def mqtt_device_candidates(self) -> list[str]:
        candidates: list[str] = []

        raw_candidates = str(self.mqtt_dispatch_devices or "").split(",")
        for item in raw_candidates:
            value = item.strip()
            if value and value not in candidates:
                candidates.append(value)

        default_device = str(self.mqtt_default_device or "").strip()
        if default_device and default_device not in candidates:
            candidates.append(default_device)

        return candidates

    def topic_cmd(self, device_name: str | None = None) -> str:
        return f"{self.mqtt_topic_prefix}/{device_name or self.mqtt_default_device}/cmd"

    def topic_ack(self, device_name: str | None = None) -> str:
        return f"{self.mqtt_topic_prefix}/{device_name or self.mqtt_default_device}/ack"

    def topic_state(self, device_name: str | None = None) -> str:
        return f"{self.mqtt_topic_prefix}/{device_name or self.mqtt_default_device}/state"


@lru_cache(maxsize=1)
def get_settings() -> Settings:
    return Settings()
