from __future__ import annotations

from dataclasses import dataclass
from datetime import UTC, datetime, timedelta

from spacex_launch_watcher.config import WatcherConfig


@dataclass(frozen=True)
class Launch:
    name: str
    provider: str
    launch_time: datetime
    mission_name: str | None = None
    mission_description: str | None = None
    pad_location_name: str | None = None


@dataclass(frozen=True)
class Alert:
    name: str
    launch: Launch
    message: str


def run_dry_watcher_cycle(config: WatcherConfig, now: datetime | None = None) -> str:
    now = datetime.now(UTC) if now is None else now
    launches = _fake_launches(now)
    alerts = [
        _create_launch_soon_alert(launch, config, now)
        for launch in launches
        if is_relevant_launch(
            launch,
            launch_provider=config.watcher.launch_provider,
            include_terms=config.watcher.include_terms,
        )
    ]
    candidate_alerts = [alert for alert in alerts if alert is not None]

    lines = ["Dry Run: one Watcher evaluation with fake launch data."]
    if not candidate_alerts:
        lines.append("No candidate Alerts.")
    for alert in candidate_alerts:
        recipients = ", ".join(recipient.name for recipient in config.recipients)
        lines.append(f"Candidate Alert: {alert.name}")
        lines.append(f"Launch: {alert.launch.name}")
        lines.append(f"Message: {alert.message}")
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
    minutes_until_launch = (launch.launch_time - now).total_seconds() / 60
    if not 0 <= minutes_until_launch <= config.alert_policy.launch_soon_minutes_before:
        return None

    launch_time = launch.launch_time.strftime("%H:%M UTC")
    return Alert(
        name="Launch Soon Alert",
        launch=launch,
        message=(
            f"Launch Soon Alert: {launch.name} is scheduled for {launch_time}. "
            "Open the Live Feed when ready."
        ),
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
