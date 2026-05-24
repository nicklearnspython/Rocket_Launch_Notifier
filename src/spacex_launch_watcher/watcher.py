from __future__ import annotations

from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
import json
import os
from pathlib import Path
from typing import Any, Protocol

from spacex_launch_watcher.config import WatcherConfig


@dataclass(frozen=True)
class Launch:
    name: str
    provider: str
    launch_time: datetime | None
    mission_name: str | None = None
    mission_description: str | None = None
    pad_location_name: str | None = None
    source_name: str = "fake"
    source_launch_id: str | None = None
    timing_precision: str = "precise"
    launch_status: str = "go"


@dataclass(frozen=True)
class Alert:
    name: str
    launch: Launch
    message: str
    correction_reason: str | None = None
    previous_launch: "LaunchSnapshot | None" = None


@dataclass(frozen=True)
class LaunchSnapshot:
    name: str
    launch_time: datetime | None
    timing_precision: str
    launch_status: str


@dataclass(frozen=True)
class AlertRecord:
    launch_key: str
    source_name: str
    source_launch_id: str
    launch_name: str
    launch_provider: str
    created_alert_names: tuple[str, ...]
    previous_launch: LaunchSnapshot
    matched_include_terms: tuple[str, ...]
    last_alert_created_at: datetime


@dataclass(frozen=True)
class DecisionResult:
    alerts: tuple[Alert, ...]
    alert_records: dict[str, AlertRecord]


class NotificationChannel(Protocol):
    def deliver(self, alert_name: str, recipient_name: str, message: str) -> None:
        pass


class FakeNotificationChannel:
    def __init__(self) -> None:
        self.deliveries: list[tuple[str, str]] = []

    def deliver(self, alert_name: str, recipient_name: str, message: str) -> None:
        self.deliveries.append((alert_name, recipient_name))


def evaluate_alerts(
    *,
    launches: tuple[Launch, ...],
    alert_records: dict[str, AlertRecord],
    config: WatcherConfig,
    now: datetime,
) -> DecisionResult:
    updated_records = dict(alert_records)
    alerts: list[Alert] = []

    for launch in launches:
        if not is_relevant_launch(
            launch,
            launch_provider=config.watcher.launch_provider,
            include_terms=config.watcher.include_terms,
        ):
            continue

        launch_key = _launch_key(launch)
        record = updated_records.get(launch_key)
        correction_alert = _correction_alert(launch, config, record)
        if correction_alert is not None:
            alerts.append(correction_alert)
            updated_records[launch_key] = _updated_record(
                launch=launch,
                existing_record=record,
                alert=correction_alert,
                config=config,
                now=now,
            )
            continue

        if not _is_inside_schedule_lookahead(
            launch, now, config.watcher.schedule_lookahead_hours
        ):
            continue

        if launch.launch_status != "go":
            continue

        minutes_until_launch = _minutes_until_launch(launch, now)
        if minutes_until_launch is None:
            continue
        alert = _new_alert_for_go_launch(launch, config, minutes_until_launch, record)
        if alert is None:
            continue

        alerts.append(alert)
        updated_records[launch_key] = _updated_record(
            launch=launch,
            existing_record=record,
            alert=alert,
            config=config,
            now=now,
        )

    return DecisionResult(alerts=tuple(alerts), alert_records=updated_records)


def load_alert_records(path: Path) -> dict[str, AlertRecord]:
    if not path.exists():
        return {}
    raw = json.loads(path.read_text(encoding="utf-8"))
    records = raw.get("alert_records", [])
    return {
        record.launch_key: record
        for record in (_alert_record_from_json(item) for item in records)
    }


def save_alert_records(path: Path, alert_records: dict[str, AlertRecord]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_name(f".{path.name}.tmp")
    payload = {
        "alert_records": [
            _alert_record_to_json(record)
            for record in sorted(alert_records.values(), key=lambda item: item.launch_key)
        ]
    }
    temp_path.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temp_path, path)


def prune_alert_records(
    alert_records: dict[str, AlertRecord], *, retention_days: int, now: datetime
) -> dict[str, AlertRecord]:
    cutoff = now - timedelta(days=retention_days)
    return {
        launch_key: record
        for launch_key, record in alert_records.items()
        if record.last_alert_created_at >= cutoff
    }


def run_watcher_cycle(
    *,
    config: WatcherConfig,
    alert_records_path: Path,
    launches: tuple[Launch, ...] | None = None,
    notification_channel: NotificationChannel | None = None,
    now: datetime | None = None,
) -> DecisionResult:
    now = datetime.now(UTC) if now is None else now.astimezone(UTC)
    launches = _fake_launches(now) if launches is None else launches
    notification_channel = (
        FakeNotificationChannel()
        if notification_channel is None
        else notification_channel
    )

    existing_records = prune_alert_records(
        load_alert_records(alert_records_path),
        retention_days=config.watcher.retention_days,
        now=now,
    )
    decision = evaluate_alerts(
        launches=launches,
        alert_records=existing_records,
        config=config,
        now=now,
    )

    if decision.alerts or decision.alert_records != existing_records:
        save_alert_records(alert_records_path, decision.alert_records)

    for alert in decision.alerts:
        from spacex_launch_watcher.notifications import format_alert

        notification = format_alert(
            alert, display_timezone=config.watcher.display_timezone
        )
        for recipient in config.recipients:
            notification_channel.deliver(alert.name, recipient.name, notification.body)

    return decision


def run_dry_watcher_cycle(config: WatcherConfig, now: datetime | None = None) -> str:
    from spacex_launch_watcher.notifications import format_alert

    now = datetime.now(UTC) if now is None else now
    decision = evaluate_alerts(
        launches=_fake_launches(now),
        alert_records={},
        config=config,
        now=now,
    )

    lines = ["Dry Run: one Watcher evaluation with fake launch data."]
    if not decision.alerts:
        lines.append("No candidate Alerts.")
    for alert in decision.alerts:
        recipients = ", ".join(recipient.name for recipient in config.recipients)
        notification = format_alert(
            alert, display_timezone=config.watcher.display_timezone
        )
        lines.append(f"Candidate Alert: {alert.name}")
        lines.append(f"Launch: {alert.launch.name}")
        lines.append(f"Message: {notification.body}")
        lines.append(f"Dry Run would send to: {recipients}")
    lines.append("No Notifications sent.")
    lines.append("No Alert Records written.")
    lines.append("No Watcher Log entries written.")
    return "\n".join(lines)


def is_relevant_launch(
    launch: Launch, *, launch_provider: str, include_terms: tuple[str, ...]
) -> bool:
    if launch.provider.casefold() != launch_provider.casefold():
        return False

    searchable_text = " ".join(_searchable_fields(launch)).casefold()
    return any(include_term.casefold() in searchable_text for include_term in include_terms)


def _fake_launches(now: datetime) -> tuple[Launch, ...]:
    return (
        Launch(
            name="Starship Flight Test",
            provider="SpaceX",
            launch_time=now + timedelta(minutes=20),
            mission_name="Starship Integrated Flight Test",
            mission_description="A fake dry-run Starship launch.",
            pad_location_name="Starbase Orbital Launch Pad",
        ),
        Launch(
            name="Unrelated Demonstration Mission",
            provider="Example Launch Provider",
            launch_time=now + timedelta(minutes=20),
            mission_name="DemoSat",
            mission_description="A fake non-SpaceX launch.",
            pad_location_name="Example Pad",
        ),
    )


def _create_launch_soon_alert(
    launch: Launch, config: WatcherConfig, now: datetime
) -> Alert | None:
    if launch.launch_time is None:
        return None
    minutes_until_launch = (launch.launch_time - now).total_seconds() / 60
    if not 0 <= minutes_until_launch <= config.alert_policy.launch_soon_minutes_before:
        return None

    return _launch_soon_alert(launch)


def _launch_soon_alert(launch: Launch) -> Alert:
    if launch.launch_time is None:
        raise ValueError("Launch Soon Alert requires a Launch Time")
    return _countdown_alert_named(launch, "Launch Soon Alert")


def _countdown_alert_named(launch: Launch, name: str) -> Alert:
    if launch.launch_time is None:
        raise ValueError(f"{name} requires a Launch Time")
    launch_time = launch.launch_time.strftime("%H:%M UTC")
    return Alert(
        name=name,
        launch=launch,
        message=(
            f"{name}: {launch.name} is scheduled for {launch_time}. "
            "Open the Live Feed when ready."
        ),
    )


def _countdown_alert(
    launch: Launch,
    config: WatcherConfig,
    minutes_until_launch: float,
    record: AlertRecord | None,
) -> Alert | None:
    created_alert_names = record.created_alert_names if record else ()
    if "Launch Imminent Alert" in created_alert_names:
        return None
    countdown_candidates: tuple[tuple[str, float], ...] = (
        ("Launch Imminent Alert", config.alert_policy.launch_imminent_minutes_before),
        ("Launch Soon Alert", config.alert_policy.launch_soon_minutes_before),
    )
    for alert_name, threshold in countdown_candidates:
        if minutes_until_launch <= threshold and alert_name not in created_alert_names:
            return _countdown_alert_named(launch, alert_name)
    return None


def _new_alert_for_go_launch(
    launch: Launch,
    config: WatcherConfig,
    minutes_until_launch: float,
    record: AlertRecord | None,
) -> Alert | None:
    if launch.timing_precision == "precise":
        if minutes_until_launch > config.alert_policy.launch_soon_minutes_before:
            return None
        return _countdown_alert(launch, config, minutes_until_launch, record)
    if launch.timing_precision == "hour":
        created_alert_names = record.created_alert_names if record else ()
        if (
            0 <= minutes_until_launch <= config.alert_policy.hour_precision_window_minutes
            and "Hour-Precision Alert" not in created_alert_names
        ):
            return _hour_precision_alert(launch)
    return None


def _hour_precision_alert(launch: Launch) -> Alert:
    if launch.launch_time is None:
        raise ValueError("Hour-Precision Alert requires a Launch Time")
    launch_time = launch.launch_time.strftime("%H:%M UTC")
    return Alert(
        name="Hour-Precision Alert",
        launch=launch,
        message=(
            f"Hour-Precision Alert: {launch.name} is possible around {launch_time}. "
            "Check the Live Feed before relying on a countdown."
        ),
    )


def _correction_alert(
    launch: Launch, config: WatcherConfig, record: AlertRecord | None
) -> Alert | None:
    if record is None:
        return None
    reason = _correction_reason(launch, config, record.previous_launch)
    if reason is None:
        return None
    return Alert(
        name="Correction Alert",
        launch=launch,
        message=f"Correction Alert: {launch.name} changed ({reason}).",
        correction_reason=reason,
        previous_launch=record.previous_launch,
    )


def _correction_reason(
    launch: Launch, config: WatcherConfig, previous_launch: LaunchSnapshot
) -> str | None:
    if launch.launch_status in {"ended", "unknown"}:
        return None
    if launch.launch_status == "no go" and previous_launch.launch_status == "go":
        return "no_go"
    if (
        previous_launch.timing_precision == "precise"
        and launch.timing_precision == "imprecise"
    ):
        return "timing_now_imprecise"
    if previous_launch.launch_time is None or launch.launch_time is None:
        return None
    time_shift_minutes = abs(
        (launch.launch_time - previous_launch.launch_time).total_seconds() / 60
    )
    if time_shift_minutes >= config.alert_policy.correction_threshold_minutes:
        return "time_changed"
    return None


def _launch_key(launch: Launch) -> str:
    source_launch_id = launch.source_launch_id or launch.name
    return f"{launch.source_name}:{source_launch_id}"


def _minutes_until_launch(launch: Launch, now: datetime) -> float | None:
    if launch.launch_time is None:
        return None
    return (launch.launch_time - now).total_seconds() / 60


def _is_inside_schedule_lookahead(
    launch: Launch, now: datetime, schedule_lookahead_hours: int
) -> bool:
    if launch.launch_time is None:
        return False
    return now <= launch.launch_time <= now + timedelta(hours=schedule_lookahead_hours)


def _snapshot(launch: Launch) -> LaunchSnapshot:
    return LaunchSnapshot(
        name=launch.name,
        launch_time=launch.launch_time,
        timing_precision=launch.timing_precision,
        launch_status=launch.launch_status,
    )


def _updated_record(
    *,
    launch: Launch,
    existing_record: AlertRecord | None,
    alert: Alert,
    config: WatcherConfig,
    now: datetime,
) -> AlertRecord:
    created_alert_names = existing_record.created_alert_names if existing_record else ()
    return AlertRecord(
        launch_key=_launch_key(launch),
        source_name=launch.source_name,
        source_launch_id=launch.source_launch_id or launch.name,
        launch_name=launch.name,
        launch_provider=launch.provider,
        created_alert_names=created_alert_names + (alert.name,),
        previous_launch=_snapshot(launch),
        matched_include_terms=_matched_include_terms(
            launch, include_terms=config.watcher.include_terms
        ),
        last_alert_created_at=now,
    )


def _alert_record_to_json(record: AlertRecord) -> dict[str, Any]:
    return {
        "launch_key": record.launch_key,
        "source_name": record.source_name,
        "source_launch_id": record.source_launch_id,
        "launch_name": record.launch_name,
        "launch_provider": record.launch_provider,
        "created_alert_names": list(record.created_alert_names),
        "previous_launch": {
            "name": record.previous_launch.name,
            "launch_time": _datetime_to_json(record.previous_launch.launch_time),
            "timing_precision": record.previous_launch.timing_precision,
            "launch_status": record.previous_launch.launch_status,
        },
        "matched_include_terms": list(record.matched_include_terms),
        "last_alert_created_at": _datetime_to_json(record.last_alert_created_at),
    }


def _alert_record_from_json(raw: dict[str, Any]) -> AlertRecord:
    previous_launch = raw["previous_launch"]
    return AlertRecord(
        launch_key=raw["launch_key"],
        source_name=raw["source_name"],
        source_launch_id=raw["source_launch_id"],
        launch_name=raw["launch_name"],
        launch_provider=raw["launch_provider"],
        created_alert_names=tuple(raw["created_alert_names"]),
        previous_launch=LaunchSnapshot(
            name=previous_launch["name"],
            launch_time=_datetime_from_json(previous_launch["launch_time"]),
            timing_precision=previous_launch["timing_precision"],
            launch_status=previous_launch["launch_status"],
        ),
        matched_include_terms=tuple(raw["matched_include_terms"]),
        last_alert_created_at=_datetime_from_json(raw["last_alert_created_at"]),
    )


def _datetime_to_json(value: datetime | None) -> str | None:
    if value is None:
        return None
    return value.astimezone(UTC).isoformat().replace("+00:00", "Z")


def _datetime_from_json(value: str | None) -> datetime | None:
    if value is None:
        return None
    return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(UTC)


def _matched_include_terms(
    launch: Launch, *, include_terms: tuple[str, ...]
) -> tuple[str, ...]:
    searchable_text = " ".join(_searchable_fields(launch)).casefold()
    return tuple(
        include_term
        for include_term in include_terms
        if include_term.casefold() in searchable_text
    )


def _searchable_fields(launch: Launch) -> tuple[str, ...]:
    return tuple(
        field
        for field in (
            launch.name,
            launch.mission_name,
            launch.mission_description,
            launch.pad_location_name,
        )
        if field
    )
