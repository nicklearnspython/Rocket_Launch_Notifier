from __future__ import annotations

import os
import tomllib
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError


DEFAULT_SOURCE_NAME = "launch-library-2"
DEFAULT_DISPLAY_TIMEZONE = "America/Los_Angeles"
DEFAULT_LAUNCH_SOON_MINUTES = 30
DEFAULT_LAUNCH_IMMINENT_MINUTES = 5
DEFAULT_HOUR_PRECISION_WINDOW_MINUTES = 60
DEFAULT_CORRECTION_THRESHOLD_MINUTES = 10
DEFAULT_POLL_INTERVAL_SECONDS = 300
DEFAULT_RETENTION_DAYS = 30


class ConfigError(ValueError):
    """Raised when startup configuration is missing or invalid."""

    def __init__(self, errors: list[str]) -> None:
        self.errors = errors
        message = "Invalid Watcher configuration:\n" + "\n".join(
            f"- {error}" for error in errors
        )
        super().__init__(message)


@dataclass(frozen=True)
class WatcherSettings:
    launch_provider: str
    include_terms: tuple[str, ...]
    display_timezone: str
    poll_interval_seconds: int
    retention_days: int


@dataclass(frozen=True)
class LaunchScheduleSourceSettings:
    name: str


@dataclass(frozen=True)
class AlertPolicySettings:
    launch_soon_minutes_before: int
    launch_imminent_minutes_before: int
    hour_precision_window_minutes: int
    correction_threshold_minutes: int


@dataclass(frozen=True)
class PushoverSettings:
    app_token_env: str
    app_token: str


@dataclass(frozen=True)
class RecipientSettings:
    name: str
    user_key_env: str
    user_key: str


@dataclass(frozen=True)
class WatcherConfig:
    watcher: WatcherSettings
    launch_schedule_source: LaunchScheduleSourceSettings
    alert_policy: AlertPolicySettings
    notification_channel: PushoverSettings
    recipients: tuple[RecipientSettings, ...]


def load_config(path: Path, env: Mapping[str, str] | None = None) -> WatcherConfig:
    env = os.environ if env is None else env
    try:
        raw_config = tomllib.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise ConfigError([f"config file not found: {path}"]) from error
    except tomllib.TOMLDecodeError as error:
        raise ConfigError([f"config file is not valid TOML: {error}"]) from error

    return parse_config(raw_config, env)


def parse_config(raw_config: Mapping[str, Any], env: Mapping[str, str]) -> WatcherConfig:
    errors: list[str] = []
    watcher_raw = _section(raw_config, "watcher", errors)
    source_raw = _section(raw_config, "launch_schedule_source", errors)
    alert_policy_raw = _section(raw_config, "alert_policy", errors)
    notification_channel_raw = _section(raw_config, "notification_channel", errors)
    recipients_raw = raw_config.get("recipients")

    launch_provider = _required_non_empty_string(
        watcher_raw, "launch_provider", "Launch Provider", errors
    )
    include_terms = _required_non_empty_string_list(
        watcher_raw, "include_terms", "Include Terms", errors
    )
    display_timezone = _optional_non_empty_string(
        watcher_raw, "display_timezone", DEFAULT_DISPLAY_TIMEZONE, "display timezone", errors
    )
    if display_timezone:
        _validate_timezone(display_timezone, errors)

    poll_interval_seconds = _positive_int(
        watcher_raw,
        "poll_interval_seconds",
        DEFAULT_POLL_INTERVAL_SECONDS,
        "poll interval",
        errors,
    )
    retention_days = _positive_int(
        watcher_raw, "retention_days", DEFAULT_RETENTION_DAYS, "retention", errors
    )

    source_name = _optional_non_empty_string(
        source_raw, "name", DEFAULT_SOURCE_NAME, "Launch Schedule Source", errors
    )

    launch_soon_minutes = _positive_int(
        alert_policy_raw,
        "launch_soon_minutes_before",
        DEFAULT_LAUNCH_SOON_MINUTES,
        "launch soon alert threshold",
        errors,
    )
    launch_imminent_minutes = _positive_int(
        alert_policy_raw,
        "launch_imminent_minutes_before",
        DEFAULT_LAUNCH_IMMINENT_MINUTES,
        "launch imminent alert threshold",
        errors,
    )
    hour_precision_window_minutes = _positive_int(
        alert_policy_raw,
        "hour_precision_window_minutes",
        DEFAULT_HOUR_PRECISION_WINDOW_MINUTES,
        "hour-precision alert threshold",
        errors,
    )
    correction_threshold_minutes = _positive_int(
        alert_policy_raw,
        "correction_threshold_minutes",
        DEFAULT_CORRECTION_THRESHOLD_MINUTES,
        "correction alert threshold",
        errors,
    )
    if (
        launch_soon_minutes is not None
        and launch_imminent_minutes is not None
        and launch_soon_minutes <= launch_imminent_minutes
    ):
        errors.append(
            "alert policy must make launch_soon_minutes_before greater than "
            "launch_imminent_minutes_before"
        )

    channel_type = _optional_non_empty_string(
        notification_channel_raw, "type", "pushover", "Notification Channel type", errors
    )
    if channel_type and channel_type != "pushover":
        errors.append("Notification Channel type must be 'pushover'")
    app_token_env = _required_non_empty_string(
        notification_channel_raw,
        "app_token_env",
        "Pushover app token environment variable",
        errors,
    )
    app_token = _lookup_secret(app_token_env, env, errors)

    recipients = _parse_recipients(recipients_raw, env, errors)

    if errors:
        raise ConfigError(errors)

    return WatcherConfig(
        watcher=WatcherSettings(
            launch_provider=launch_provider,
            include_terms=tuple(include_terms),
            display_timezone=display_timezone,
            poll_interval_seconds=poll_interval_seconds,
            retention_days=retention_days,
        ),
        launch_schedule_source=LaunchScheduleSourceSettings(name=source_name),
        alert_policy=AlertPolicySettings(
            launch_soon_minutes_before=launch_soon_minutes,
            launch_imminent_minutes_before=launch_imminent_minutes,
            hour_precision_window_minutes=hour_precision_window_minutes,
            correction_threshold_minutes=correction_threshold_minutes,
        ),
        notification_channel=PushoverSettings(
            app_token_env=app_token_env,
            app_token=app_token,
        ),
        recipients=tuple(recipients),
    )


def _section(
    raw_config: Mapping[str, Any], section_name: str, errors: list[str]
) -> Mapping[str, Any]:
    section = raw_config.get(section_name, {})
    if not isinstance(section, dict):
        errors.append(f"{section_name} must be a TOML table")
        return {}
    return section


def _required_non_empty_string(
    section: Mapping[str, Any], key: str, label: str, errors: list[str]
) -> str | None:
    value = section.get(key)
    if not isinstance(value, str) or not value.strip():
        errors.append(f"missing {label}")
        return None
    return value.strip()


def _optional_non_empty_string(
    section: Mapping[str, Any],
    key: str,
    default: str,
    label: str,
    errors: list[str],
) -> str | None:
    value = section.get(key, default)
    if not isinstance(value, str) or not value.strip():
        errors.append(f"{label} must be a non-empty string")
        return None
    return value.strip()


def _required_non_empty_string_list(
    section: Mapping[str, Any], key: str, label: str, errors: list[str]
) -> list[str] | None:
    value = section.get(key)
    if not isinstance(value, list):
        errors.append(f"missing {label}")
        return None
    cleaned = [item.strip() for item in value if isinstance(item, str) and item.strip()]
    if len(cleaned) != len(value) or not cleaned:
        errors.append(f"{label} must contain at least one non-empty string")
        return None
    return cleaned


def _positive_int(
    section: Mapping[str, Any],
    key: str,
    default: int,
    label: str,
    errors: list[str],
) -> int | None:
    value = section.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        errors.append(f"{label} must be a positive integer")
        return None
    return value


def _validate_timezone(display_timezone: str, errors: list[str]) -> None:
    try:
        ZoneInfo(display_timezone)
    except ZoneInfoNotFoundError:
        errors.append(f"invalid display timezone: {display_timezone}")


def _lookup_secret(
    env_var: str | None, env: Mapping[str, str], errors: list[str]
) -> str | None:
    if env_var is None:
        return None
    value = env.get(env_var)
    if not value:
        errors.append(f"missing referenced secret environment variable: {env_var}")
        return None
    return value


def _parse_recipients(
    recipients_raw: Any, env: Mapping[str, str], errors: list[str]
) -> list[RecipientSettings] | None:
    if not isinstance(recipients_raw, list) or not recipients_raw:
        errors.append("missing recipients")
        return None

    recipients: list[RecipientSettings] = []
    for index, raw_recipient in enumerate(recipients_raw, start=1):
        if not isinstance(raw_recipient, dict):
            errors.append(f"recipient {index} must be a TOML table")
            continue
        name = _required_non_empty_string(
            raw_recipient, "name", f"recipient {index} name", errors
        )
        user_key_env = _required_non_empty_string(
            raw_recipient,
            "user_key_env",
            f"recipient {index} user key environment variable",
            errors,
        )
        user_key = _lookup_secret(user_key_env, env, errors)
        if name and user_key_env and user_key:
            recipients.append(
                RecipientSettings(name=name, user_key_env=user_key_env, user_key=user_key)
            )

    return recipients
