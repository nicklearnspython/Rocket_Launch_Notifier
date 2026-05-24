from __future__ import annotations

from datetime import UTC, datetime, timedelta
from urllib.parse import parse_qs, urlparse
import unittest

from spacex_launch_watcher.launch_library_2 import LaunchLibrary2LaunchScheduleSource


NOW = datetime(2026, 5, 24, 12, 0, tzinfo=UTC)


def source_payload(*, status: dict, precision: dict | None) -> dict:
    return {
        "id": "936d3e4c-2f1f-40ce-a6ef-starship10",
        "name": "Starship | Flight 10",
        "status": status,
        "net": "2026-05-24T12:20:00Z",
        "net_precision": precision,
        "launch_service_provider": {"name": "SpaceX"},
        "mission": {
            "name": "Starship Integrated Flight Test",
            "description": "Launch and splashdown test from South Texas.",
        },
        "pad": {
            "name": "Orbital Launch Pad A",
            "location": {"name": "Starbase, TX, USA"},
        },
    }


class LaunchLibrary2LaunchScheduleSourceTests(unittest.TestCase):
    def test_fetches_practical_future_range_and_maps_payload_to_contract(self) -> None:
        requested_urls: list[str] = []

        def http_get(url: str) -> dict:
            requested_urls.append(url)
            return {
                "results": [
                    source_payload(
                        status={"id": 1, "name": "Go for Launch", "abbrev": "Go"},
                        precision={"id": 1, "name": "Minute", "abbrev": "MIN"},
                    )
                ]
            }

        source = LaunchLibrary2LaunchScheduleSource(
            schedule_lookahead_hours=30,
            http_get=http_get,
        )

        result = source.fetch_launches(NOW)

        self.assertEqual(result.fetched_at, NOW)
        self.assertEqual(result.warnings, ())
        launch = result.launches[0]
        self.assertEqual(launch.source_name, "launch-library-2")
        self.assertEqual(
            launch.source_launch_id, "936d3e4c-2f1f-40ce-a6ef-starship10"
        )
        self.assertEqual(launch.name, "Starship | Flight 10")
        self.assertEqual(launch.provider, "SpaceX")
        self.assertEqual(launch.launch_time, NOW + timedelta(minutes=20))
        self.assertEqual(launch.timing_precision, "precise")
        self.assertEqual(launch.launch_status, "go")
        self.assertEqual(launch.mission_name, "Starship Integrated Flight Test")
        self.assertEqual(
            launch.mission_description,
            "Launch and splashdown test from South Texas.",
        )
        self.assertEqual(launch.pad_location_name, "Starbase, TX, USA")

        query = parse_qs(urlparse(requested_urls[0]).query)
        self.assertEqual(query["mode"], ["normal"])
        self.assertEqual(query["limit"], ["100"])
        self.assertEqual(query["ordering"], ["net"])
        self.assertEqual(query["hide_recent_previous"], ["True"])
        self.assertEqual(query["net__gte"], ["2026-05-24T12:00:00Z"])
        self.assertEqual(query["net__lte"], ["2026-05-25T18:00:00Z"])

    def test_maps_hour_precision_and_non_go_statuses_conservatively(self) -> None:
        def http_get(url: str) -> dict:
            return {
                "results": [
                    source_payload(
                        status={
                            "id": 2,
                            "name": "To Be Determined",
                            "abbrev": "TBD",
                        },
                        precision={"id": 2, "name": "Hour", "abbrev": "HR"},
                    )
                ]
            }

        result = LaunchLibrary2LaunchScheduleSource(http_get=http_get).fetch_launches(NOW)

        self.assertEqual(result.launches[0].timing_precision, "hour")
        self.assertEqual(result.launches[0].launch_status, "no go")
        self.assertEqual(result.warnings, ())

    def test_malformed_and_unmapped_values_return_warnings_and_unknowns(self) -> None:
        def http_get(url: str) -> dict:
            return {
                "results": [
                    {
                        "id": "bad-source-values",
                        "name": "Falcon 9 | Mystery Payload",
                        "status": {"id": 999, "name": "Maybe", "abbrev": "??"},
                        "net": "not-a-date",
                        "net_precision": {"id": 999, "name": "Fortnight"},
                        "launch_service_provider": {"name": "SpaceX"},
                        "mission": {},
                        "pad": {},
                    }
                ]
            }

        result = LaunchLibrary2LaunchScheduleSource(http_get=http_get).fetch_launches(NOW)

        self.assertEqual(result.launches[0].launch_time, None)
        self.assertEqual(result.launches[0].timing_precision, "unknown")
        self.assertEqual(result.launches[0].launch_status, "unknown")
        self.assertEqual(
            result.warnings,
            (
                "Launch Library 2 launch bad-source-values has malformed Launch Time: not-a-date",
                "Launch Library 2 launch bad-source-values has unmapped Timing Precision: Fortnight",
                "Launch Library 2 launch bad-source-values has unmapped Launch Status: Maybe",
            ),
        )


if __name__ == "__main__":
    unittest.main()
