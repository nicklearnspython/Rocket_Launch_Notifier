from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from zoneinfo import ZoneInfo

from spacex_launch_watcher.watcher import Alert


@dataclass(frozen=True)
class FormattedNotification:
    title: str
    body: str
    metadata: dict[str, str]


def format_alert(alert: Alert, *, display_timezone: str) -> FormattedNotification:
    display_zone = ZoneInfo(display_timezone)
    if alert.name == "Correction Alert":
        title, body = _format_correction_alert(alert, display_zone)
        return FormattedNotification(
            title=title,
            body=body,
            metadata=_metadata(alert),
        )

    launch_time = alert.launch.launch_time
    if launch_time is None:
        raise ValueError(f"{alert.name} requires a Launch Time")

    displayed_launch_time = _display_time(launch_time, display_zone)
    title_prefix, body_phrase = _recipient_wording(alert)
    title = f"{title_prefix}: {alert.launch.name}"
    body = f"{alert.launch.name} {body_phrase}\nLaunch Time: {displayed_launch_time}"
    return FormattedNotification(
        title=title,
        body=body,
        metadata=_metadata(alert),
    )


def _recipient_wording(alert: Alert) -> tuple[str, str]:
    if alert.name == "Launch Imminent Alert":
        return "Launch Imminent", "is scheduled now."
    if alert.name == "Hour-Precision Alert":
        return "Possible Launch This Hour", "is a possible launch this hour."
    return "Launch Soon", "is scheduled soon."


def _format_correction_alert(alert: Alert, display_zone: ZoneInfo) -> tuple[str, str]:
    if alert.correction_reason == "time_changed":
        if alert.previous_launch is None:
            raise ValueError("time_changed Correction Alert requires previous launch")
        if alert.previous_launch.launch_time is None or alert.launch.launch_time is None:
            raise ValueError("time_changed Correction Alert requires Launch Times")
        return (
            f"Launch Time Changed: {alert.launch.name}",
            (
                f"{alert.launch.name} has a changed Launch Time.\n"
                f"Previous Launch Time: "
                f"{_display_time(alert.previous_launch.launch_time, display_zone)}\n"
                f"Current Launch Time: "
                f"{_display_time(alert.launch.launch_time, display_zone)}"
            ),
        )
    if alert.correction_reason == "timing_now_imprecise":
        if alert.launch.launch_time is None:
            raise ValueError("timing_now_imprecise Correction Alert requires Launch Time")
        return (
            f"Launch Time Less Precise: {alert.launch.name}",
            (
                f"The schedule no longer has a precise Launch Time.\n"
                f"Launch Time: {_display_time(alert.launch.launch_time, display_zone)}"
            ),
        )
    if alert.correction_reason == "no_go":
        if alert.launch.launch_time is None:
            raise ValueError("no_go Correction Alert requires Launch Time")
        return (
            f"Launch No Go: {alert.launch.name}",
            (
                f"The launch is no longer go.\n"
                f"Launch Time: {_display_time(alert.launch.launch_time, display_zone)}"
            ),
        )
    return (
        f"Launch Correction: {alert.launch.name}",
        f"{alert.launch.name} has an important launch update.",
    )


def _display_time(value: datetime, display_zone: ZoneInfo) -> str:
    displayed = value.astimezone(display_zone)
    hour = displayed.strftime("%I").lstrip("0") or "0"
    return f"{displayed:%Y-%m-%d} {hour}:{displayed:%M %p %Z}"


def _metadata(alert: Alert) -> dict[str, str]:
    metadata = {
        "alert_kind": alert.name,
        "source_name": alert.launch.source_name,
        "source_launch_id": alert.launch.source_launch_id or alert.launch.name,
        "launch_name": alert.launch.name,
    }
    if alert.correction_reason is not None:
        metadata["correction_reason"] = alert.correction_reason
    return metadata
