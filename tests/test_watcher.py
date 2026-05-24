from __future__ import annotations

from datetime import UTC, datetime, timedelta
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from spacex_launch_watcher.config import parse_config
from spacex_launch_watcher.watcher import (
    AlertRecord,
    Launch,
    LaunchSnapshot,
    evaluate_alerts,
    is_relevant_launch,
    load_alert_records,
    prune_alert_records,
    run_watcher_cycle,
    save_alert_records,
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


def alert_record(
    *,
    launch_key: str = "launch-library-2:starship-flight-10",
    last_alert_created_at: datetime,
) -> AlertRecord:
    return AlertRecord(
        launch_key=launch_key,
        source_name="launch-library-2",
        source_launch_id=launch_key.split(":", 1)[1],
        launch_name="Starship Flight Test",
        launch_provider="SpaceX",
        created_alert_names=("Launch Soon Alert",),
        previous_launch=LaunchSnapshot(
            name="Starship Flight Test",
            launch_time=NOW + timedelta(minutes=20),
            timing_precision="precise",
            launch_status="go",
        ),
        matched_include_terms=("Starship",),
        last_alert_created_at=last_alert_created_at,
    )


class RecordingNotificationChannel:
    def __init__(self) -> None:
        self.deliveries: list[tuple[str, str]] = []

    def deliver(self, alert_name: str, recipient_name: str, message: str) -> None:
        self.deliveries.append((alert_name, recipient_name))


class WatcherTests(unittest.TestCase):
    def test_alert_records_round_trip_through_single_json_file_with_utc_timestamps(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            records_path = Path(temp_dir) / "alert-records.json"
            record = AlertRecord(
                launch_key="launch-library-2:starship-flight-10",
                source_name="launch-library-2",
                source_launch_id="starship-flight-10",
                launch_name="Starship Flight Test",
                launch_provider="SpaceX",
                created_alert_names=("Launch Soon Alert",),
                previous_launch=LaunchSnapshot(
                    name="Starship Flight Test",
                    launch_time=NOW + timedelta(minutes=20),
                    timing_precision="precise",
                    launch_status="go",
                ),
                matched_include_terms=("Starship",),
                last_alert_created_at=NOW,
            )

            save_alert_records(records_path, {record.launch_key: record})

            text = records_path.read_text(encoding="utf-8")
            self.assertIn('"last_alert_created_at": "2026-05-24T12:00:00Z"', text)
            self.assertIn('"launch_time": "2026-05-24T12:20:00Z"', text)
            self.assertNotIn("+00:00", text)
            self.assertEqual(
                load_alert_records(records_path), {record.launch_key: record}
            )

    def test_save_alert_records_preserves_existing_file_when_atomic_replace_fails(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            records_path = Path(temp_dir) / "alert-records.json"
            records_path.write_text('{"alert_records": []}\n', encoding="utf-8")
            record = alert_record(last_alert_created_at=NOW)

            with patch(
                "spacex_launch_watcher.watcher.os.replace",
                side_effect=OSError("disk full"),
            ):
                with self.assertRaises(OSError):
                    save_alert_records(records_path, {record.launch_key: record})

            self.assertEqual(
                records_path.read_text(encoding="utf-8"),
                '{"alert_records": []}\n',
            )

    def test_alert_records_are_pruned_after_configured_retention_period(self) -> None:
        recent_record = alert_record(
            launch_key="launch-library-2:recent",
            last_alert_created_at=NOW - timedelta(days=29, hours=23),
        )
        expired_record = alert_record(
            launch_key="launch-library-2:expired",
            last_alert_created_at=NOW - timedelta(days=31),
        )

        pruned = prune_alert_records(
            {
                recent_record.launch_key: recent_record,
                expired_record.launch_key: expired_record,
            },
            retention_days=30,
            now=NOW,
        )

        self.assertEqual(pruned, {recent_record.launch_key: recent_record})

    def test_watcher_cycle_saves_records_before_notification_delivery(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            records_path = Path(temp_dir) / "alert-records.json"
            notification_channel = RecordingNotificationChannel()

            result = run_watcher_cycle(
                config=watcher_config(),
                alert_records_path=records_path,
                launches=(launch(),),
                notification_channel=notification_channel,
                now=NOW,
            )

            self.assertEqual(
                [alert.name for alert in result.alerts], ["Launch Soon Alert"]
            )
            self.assertEqual(
                notification_channel.deliveries,
                [("Launch Soon Alert", "Nick")],
            )
            saved_records = load_alert_records(records_path)
            record = saved_records["launch-library-2:starship-flight-10"]
            self.assertEqual(record.launch_name, "Starship Flight Test")
            self.assertEqual(record.launch_provider, "SpaceX")
            self.assertEqual(record.source_name, "launch-library-2")
            self.assertEqual(record.source_launch_id, "starship-flight-10")
            self.assertEqual(record.matched_include_terms, ("Starship",))
            self.assertEqual(record.created_alert_names, ("Launch Soon Alert",))
            self.assertEqual(record.last_alert_created_at, NOW)

    def test_watcher_cycle_does_not_deliver_notifications_when_record_save_fails(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            records_path = Path(temp_dir) / "alert-records.json"
            notification_channel = RecordingNotificationChannel()

            with patch(
                "spacex_launch_watcher.watcher.save_alert_records",
                side_effect=OSError("disk full"),
            ):
                with self.assertRaises(OSError):
                    run_watcher_cycle(
                        config=watcher_config(),
                        alert_records_path=records_path,
                        launches=(launch(),),
                        notification_channel=notification_channel,
                        now=NOW,
                    )

            self.assertEqual(notification_channel.deliveries, [])

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
