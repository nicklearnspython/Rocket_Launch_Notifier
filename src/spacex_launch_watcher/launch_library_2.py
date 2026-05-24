from __future__ import annotations

from collections.abc import Callable, Mapping
from datetime import UTC, datetime, timedelta
import json
from typing import Any
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from spacex_launch_watcher.watcher import Launch, LaunchScheduleResult, SourceFailure


DEFAULT_BASE_URL = "https://ll.thespacedevs.com/2.3.0/launches/upcoming/"
DEFAULT_SOURCE_NAME = "launch-library-2"
DEFAULT_TIMEOUT_SECONDS = 20


class LaunchLibrary2LaunchScheduleSource:
    def __init__(
        self,
        *,
        schedule_lookahead_hours: int = 30,
        base_url: str = DEFAULT_BASE_URL,
        source_name: str = DEFAULT_SOURCE_NAME,
        http_get: Callable[[str], Mapping[str, Any]] | None = None,
    ) -> None:
        self.schedule_lookahead_hours = schedule_lookahead_hours
        self.base_url = base_url
        self.source_name = source_name
        self._http_get = _default_http_get if http_get is None else http_get

    def fetch_launches(self, now: datetime) -> LaunchScheduleResult:
        fetched_at = now.astimezone(UTC)
        payload = self._fetch_payload(fetched_at)
        raw_launches = payload.get("results", [])
        if not isinstance(raw_launches, list):
            raise SourceFailure("Launch Library 2 response did not contain results")

        launches: list[Launch] = []
        warnings: list[str] = []
        for raw_launch in raw_launches:
            if not isinstance(raw_launch, Mapping):
                warnings.append("Launch Library 2 returned a malformed launch item")
                continue
            launches.append(self._map_launch(raw_launch, warnings))

        return LaunchScheduleResult(
            launches=tuple(launches),
            warnings=tuple(warnings),
            fetched_at=fetched_at,
        )

    def _fetch_payload(self, now: datetime) -> Mapping[str, Any]:
        url = self._schedule_url(now)
        try:
            payload = self._http_get(url)
        except (OSError, URLError, TimeoutError) as error:
            raise SourceFailure(f"Launch Library 2 request failed: {error}") from error
        except json.JSONDecodeError as error:
            raise SourceFailure("Launch Library 2 returned invalid JSON") from error
        if not isinstance(payload, Mapping):
            raise SourceFailure("Launch Library 2 response was not a JSON object")
        return payload

    def _schedule_url(self, now: datetime) -> str:
        end = now + timedelta(hours=self.schedule_lookahead_hours)
        query = urlencode(
            {
                "format": "json",
                "mode": "normal",
                "limit": "100",
                "ordering": "net",
                "hide_recent_previous": "True",
                "net__gte": _datetime_to_source_query(now),
                "net__lte": _datetime_to_source_query(end),
            }
        )
        return f"{self.base_url}?{query}"

    def _map_launch(
        self, raw_launch: Mapping[str, Any], warnings: list[str]
    ) -> Launch:
        source_launch_id = _string_value(raw_launch.get("id")) or _string_value(
            raw_launch.get("slug")
        )
        launch_label = source_launch_id or _string_value(raw_launch.get("name")) or "unknown"

        launch_time = _parse_launch_time(raw_launch.get("net"), launch_label, warnings)
        timing_precision = _map_timing_precision(
            raw_launch.get("net_precision"), launch_label, warnings
        )
        launch_status = _map_launch_status(raw_launch.get("status"), launch_label, warnings)
        mission = _mapping_value(raw_launch.get("mission"))
        pad = _mapping_value(raw_launch.get("pad"))
        location = _mapping_value(pad.get("location"))
        provider = _mapping_value(raw_launch.get("launch_service_provider"))

        return Launch(
            name=_string_value(raw_launch.get("name")) or "Unknown Launch",
            provider=_string_value(provider.get("name")) or "Unknown Launch Provider",
            launch_time=launch_time,
            mission_name=_string_value(mission.get("name")),
            mission_description=_string_value(mission.get("description")),
            pad_location_name=(
                _string_value(location.get("name")) or _string_value(pad.get("name"))
            ),
            source_name=self.source_name,
            source_launch_id=source_launch_id,
            timing_precision=timing_precision,
            launch_status=launch_status,
        )


def _default_http_get(url: str) -> Mapping[str, Any]:
    request = Request(url, headers={"User-Agent": "RocketLaunchNotifier/0.1"})
    with urlopen(request, timeout=DEFAULT_TIMEOUT_SECONDS) as response:
        return json.loads(response.read().decode("utf-8"))


def _datetime_to_source_query(value: datetime) -> str:
    return value.astimezone(UTC).isoformat().replace("+00:00", "Z")


def _parse_launch_time(
    raw_value: Any, launch_label: str, warnings: list[str]
) -> datetime | None:
    if raw_value is None:
        return None
    if not isinstance(raw_value, str):
        warnings.append(
            f"Launch Library 2 launch {launch_label} has malformed Launch Time: {raw_value}"
        )
        return None
    try:
        return datetime.fromisoformat(raw_value.replace("Z", "+00:00")).astimezone(UTC)
    except ValueError:
        warnings.append(
            f"Launch Library 2 launch {launch_label} has malformed Launch Time: {raw_value}"
        )
        return None


def _map_timing_precision(
    raw_precision: Any, launch_label: str, warnings: list[str]
) -> str:
    precision = _mapping_value(raw_precision)
    precision_name = _string_value(precision.get("name"))
    precision_id = precision.get("id")
    if precision_name in {"Second", "Minute"} or precision_id in {0, 1}:
        return "precise"
    if precision_name == "Hour" or precision_id == 2:
        return "hour"
    if precision_name in {
        "Day",
        "Week",
        "Month",
        "Quarter 1",
        "Quarter 2",
        "Quarter 3",
        "Quarter 4",
        "Half 1",
        "Half 2",
        "Year",
    }:
        return "imprecise"

    label = precision_name or str(raw_precision)
    warnings.append(
        f"Launch Library 2 launch {launch_label} has unmapped Timing Precision: {label}"
    )
    return "unknown"


def _map_launch_status(raw_status: Any, launch_label: str, warnings: list[str]) -> str:
    status = _mapping_value(raw_status)
    status_id = status.get("id")
    status_name = _string_value(status.get("name"))
    status_abbrev = _string_value(status.get("abbrev"))
    status_key = status_name or status_abbrev

    if status_id == 1 or status_key == "Go for Launch":
        return "go"
    if status_id in {2, 8, 5} or status_key in {
        "To Be Determined",
        "To Be Confirmed",
        "On Hold",
    }:
        return "no go"
    if status_id in {3, 4, 6, 7, 9} or status_key in {
        "Launch Successful",
        "Launch Failure",
        "Launch in Flight",
        "Launch was a Partial Failure",
        "Payload Deployed",
    }:
        return "ended"

    label = status_key or str(raw_status)
    warnings.append(
        f"Launch Library 2 launch {launch_label} has unmapped Launch Status: {label}"
    )
    return "unknown"


def _mapping_value(raw_value: Any) -> Mapping[str, Any]:
    if isinstance(raw_value, Mapping):
        return raw_value
    return {}


def _string_value(raw_value: Any) -> str | None:
    if isinstance(raw_value, str) and raw_value.strip():
        return raw_value.strip()
    return None
