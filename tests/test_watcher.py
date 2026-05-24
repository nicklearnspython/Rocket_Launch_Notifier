from __future__ import annotations

from datetime import UTC, datetime, timedelta
import unittest

from spacex_launch_watcher.config import parse_config
from spacex_launch_watcher.watcher import (
    Launch,
    LaunchSnapshot,
    evaluate_alerts,
    is_relevant_launch,
)


NOW = datetime(2026, 5, 24, 12, 0, tzinfo=UTC)


def watcher_config():
    return parse_config(
        {
            "watcher": {
                "launch_provider": "SpaceX",
                "include_terms": ["Starship", "Vandenberg"],
            },
            "alert_policy": {
                "launch_soon_minutes_before": 30,
                "launch_imminent_minutes_before": 5,
                "hour_precision_window_minutes": 60,
                "correction_threshold_minutes": 30,
            },
            "notification_channel": {
                "type": "pushover",
                "app_token_env": "PUSHOVER_APP_TOKEN",
            },
            "recipients": [
                {
                    "name": "Nick",
                    "user_key_env": "PUSHOVER_USER_KEY_NICK",
                }
            ],
        },
        {
            "PUSHOVER_APP_TOKEN": "app-token",
            "PUSHOVER_USER_KEY_NICK": "user-key",
        },
    )


def watcher_config_with_schedule_lookahead(hours: int):
    raw_config = {
        "watcher": {
            "launch_provider": "SpaceX",
            "include_terms": ["Starship", "Vandenberg"],
            "schedule_lookahead_hours": hours,
        },
        "alert_policy": {
            "launch_soon_minutes_before": 30,
            "launch_imminent_minutes_before": 5,
            "hour_precision_window_minutes": 60,
            "correction_threshold_minutes": 30,
        },
        "notification_channel": {
            "type": "pushover",
            "app_token_env": "PUSHOVER_APP_TOKEN",
        },
        "recipients": [
            {
                "name": "Nick",
                "user_key_env": "PUSHOVER_USER_KEY_NICK",
            }
        ],
    }
    return parse_config(
        raw_config,
        {
            "PUSHOVER_APP_TOKEN": "app-token",
            "PUSHOVER_USER_KEY_NICK": "user-key",
        },
    )


def launch(
    *,
    launch_time: datetime | None = NOW + timedelta(minutes=20),
    timing_precision: str = "precise",
    launch_status: str = "go",
    source_launch_id: str = "starship-flight-10",
) -> Launch:
    return Launch(
        name="Starship Flight Test",
        provider="SpaceX",
        launch_time=launch_time,
        mission_name="Starship Integrated Flight Test",
        pad_location_name="Starbase Orbital Launch Pad",
        source_name="launch-library-2",
        source_launch_id=source_launch_id,
        timing_precision=timing_precision,
        launch_status=launch_status,
    )


class WatcherTests(unittest.TestCase):
    def test_precise_go_launch_inside_launch_soon_threshold_creates_alert_and_record(
        self,
    ) -> None:
        result = evaluate_alerts(
            launches=(launch(),),
            alert_records={},
            config=watcher_config(),
            now=NOW,
        )

        self.assertEqual([alert.name for alert in result.alerts], ["Launch Soon Alert"])
        self.assertEqual(result.alerts[0].launch.name, "Starship Flight Test")
        self.assertEqual(
            set(result.alert_records), {"launch-library-2:starship-flight-10"}
        )
        record = result.alert_records["launch-library-2:starship-flight-10"]
        self.assertEqual(record.created_alert_names, ("Launch Soon Alert",))
        self.assertEqual(record.previous_launch.name, "Starship Flight Test")
        self.assertEqual(record.previous_launch.launch_time, NOW + timedelta(minutes=20))

    def test_countdown_alerts_create_most_urgent_unsent_alert_only(self) -> None:
        result = evaluate_alerts(
            launches=(launch(launch_time=NOW + timedelta(minutes=4)),),
            alert_records={},
            config=watcher_config(),
            now=NOW,
        )

        self.assertEqual(
            [alert.name for alert in result.alerts], ["Launch Imminent Alert"]
        )
        record = result.alert_records["launch-library-2:starship-flight-10"]
        self.assertEqual(record.created_alert_names, ("Launch Imminent Alert",))

        duplicate_result = evaluate_alerts(
            launches=(launch(launch_time=NOW + timedelta(minutes=4)),),
            alert_records=result.alert_records,
            config=watcher_config(),
            now=NOW,
        )

        self.assertEqual(duplicate_result.alerts, ())
        self.assertEqual(
            duplicate_result.alert_records[
                "launch-library-2:starship-flight-10"
            ].created_alert_names,
            ("Launch Imminent Alert",),
        )

    def test_hour_precision_alert_does_not_suppress_later_countdown_alerts(self) -> None:
        hour_result = evaluate_alerts(
            launches=(
                launch(
                    launch_time=NOW + timedelta(minutes=45),
                    timing_precision="hour",
                ),
            ),
            alert_records={},
            config=watcher_config(),
            now=NOW,
        )

        self.assertEqual(
            [alert.name for alert in hour_result.alerts], ["Hour-Precision Alert"]
        )

        precise_result = evaluate_alerts(
            launches=(launch(launch_time=NOW + timedelta(minutes=30)),),
            alert_records=hour_result.alert_records,
            config=watcher_config(),
            now=NOW + timedelta(minutes=25),
        )

        self.assertEqual(
            [alert.name for alert in precise_result.alerts],
            ["Launch Imminent Alert"],
        )
        self.assertEqual(
            precise_result.alert_records[
                "launch-library-2:starship-flight-10"
            ].created_alert_names,
            ("Hour-Precision Alert", "Launch Imminent Alert"),
        )

    def test_silent_policy_for_precision_status_and_schedule_lookahead(self) -> None:
        cases = [
            launch(timing_precision="imprecise"),
            launch(timing_precision="unknown"),
            launch(launch_status="ended"),
            launch(launch_status="unknown"),
            launch(launch_time=NOW + timedelta(hours=3)),
        ]

        for candidate in cases:
            with self.subTest(candidate=candidate):
                result = evaluate_alerts(
                    launches=(candidate,),
                    alert_records={},
                    config=watcher_config_with_schedule_lookahead(2),
                    now=NOW,
                )

                self.assertEqual(result.alerts, ())
                self.assertEqual(result.alert_records, {})

    def test_correction_alerts_carry_reason_and_previous_launch_snapshot(self) -> None:
        prior_result = evaluate_alerts(
            launches=(launch(launch_time=NOW + timedelta(minutes=20)),),
            alert_records={},
            config=watcher_config(),
            now=NOW,
        )
        cases = [
            (launch(launch_status="no go"), "no_go"),
            (launch(launch_time=NOW + timedelta(minutes=55)), "time_changed"),
            (launch(timing_precision="imprecise"), "timing_now_imprecise"),
        ]

        for changed_launch, expected_reason in cases:
            with self.subTest(expected_reason=expected_reason):
                result = evaluate_alerts(
                    launches=(changed_launch,),
                    alert_records=prior_result.alert_records,
                    config=watcher_config(),
                    now=NOW,
                )

                self.assertEqual(
                    [alert.name for alert in result.alerts], ["Correction Alert"]
                )
                self.assertEqual(result.alerts[0].correction_reason, expected_reason)
                self.assertEqual(
                    result.alerts[0].previous_launch,
                    LaunchSnapshot(
                        name="Starship Flight Test",
                        launch_time=NOW + timedelta(minutes=20),
                        timing_precision="precise",
                        launch_status="go",
                    ),
                )

    def test_correction_alerts_are_not_suppressed_by_schedule_lookahead(self) -> None:
        prior_result = evaluate_alerts(
            launches=(launch(launch_time=NOW + timedelta(minutes=20)),),
            alert_records={},
            config=watcher_config(),
            now=NOW,
        )

        result = evaluate_alerts(
            launches=(launch(launch_time=NOW + timedelta(hours=31)),),
            alert_records=prior_result.alert_records,
            config=watcher_config(),
            now=NOW,
        )

        self.assertEqual([alert.name for alert in result.alerts], ["Correction Alert"])
        self.assertEqual(result.alerts[0].correction_reason, "time_changed")

    def test_alert_record_updates_do_not_mutate_input_records_in_place(self) -> None:
        prior_result = evaluate_alerts(
            launches=(launch(launch_time=NOW + timedelta(minutes=20)),),
            alert_records={},
            config=watcher_config(),
            now=NOW,
        )
        original_records = prior_result.alert_records
        original_record = original_records["launch-library-2:starship-flight-10"]

        updated_result = evaluate_alerts(
            launches=(launch(launch_time=NOW + timedelta(minutes=4)),),
            alert_records=original_records,
            config=watcher_config(),
            now=NOW,
        )

        self.assertIsNot(updated_result.alert_records, original_records)
        self.assertIs(
            original_records["launch-library-2:starship-flight-10"], original_record
        )
        self.assertEqual(original_record.created_alert_names, ("Launch Soon Alert",))
        self.assertEqual(
            updated_result.alert_records[
                "launch-library-2:starship-flight-10"
            ].created_alert_names,
            ("Launch Soon Alert", "Launch Imminent Alert"),
        )

    def test_relevant_launch_requires_configured_provider_and_any_include_term_match(
        self,
    ) -> None:
        launch = Launch(
            name="Starship Flight Test",
            provider="SpaceX",
            launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
        )

        self.assertTrue(
            is_relevant_launch(
                launch, launch_provider="SpaceX", include_terms=("Starship",)
            )
        )
        self.assertFalse(
            is_relevant_launch(
                launch, launch_provider="Rocket Lab", include_terms=("Starship",)
            )
        )
        # This launch is a Starship launch, but it is not from Vandenberg.
        # Provider alone is not enough; the configured Include Term must match too.
        self.assertFalse(
            is_relevant_launch(
                launch, launch_provider="SpaceX", include_terms=("Vandenberg",)
            )
        )

    def test_starship_launches_match_any_location_when_starship_is_configured(
        self,
    ) -> None:
        launch = Launch(
            name="Starship Flight Test",
            provider="SpaceX",
            launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
            pad_location_name="Starbase Orbital Launch Pad",
        )

        self.assertTrue(
            is_relevant_launch(
                launch,
                launch_provider="SpaceX",
                include_terms=("Starship", "Vandenberg"),
            )
        )

    def test_falcon_9_launches_match_when_launching_from_vandenberg(self) -> None:
        launch = Launch(
            name="Falcon 9 Block 5",
            provider="SpaceX",
            launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
            pad_location_name="Vandenberg Space Force Base",
        )

        self.assertTrue(
            is_relevant_launch(
                launch,
                launch_provider="SpaceX",
                include_terms=("Starship", "Vandenberg"),
            )
        )

    def test_relevance_matching_is_case_insensitive_across_searchable_fields(
        self,
    ) -> None:
        cases = [
            Launch(
                name="STARSHIP Flight Test",
                provider="spacex",
                launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
            ),
            Launch(
                name="Transporter",
                provider="spacex",
                launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
                mission_name="Starship payload demonstration",
            ),
            Launch(
                name="Falcon 9",
                provider="spacex",
                launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
                mission_description="Mission from Boca Chica.",
            ),
            Launch(
                name="Falcon 9",
                provider="spacex",
                launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
                pad_location_name="Vandenberg Space Force Base",
            ),
        ]

        for launch in cases:
            with self.subTest(launch=launch):
                self.assertTrue(
                    is_relevant_launch(
                        launch,
                        launch_provider="SPACEX",
                        include_terms=("starship", "boca chica", "vandenberg"),
                    )
                )


if __name__ == "__main__":
    unittest.main()
