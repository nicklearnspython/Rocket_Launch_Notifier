from __future__ import annotations

from datetime import UTC, datetime
import unittest

from spacex_launch_watcher.notifications import format_alert
from spacex_launch_watcher.watcher import Alert, Launch, LaunchSnapshot


class NotificationFormattingTests(unittest.TestCase):
    def test_launch_soon_message_includes_launch_name_and_display_timezone_time(
        self,
    ) -> None:
        alert = Alert(
            name="Launch Soon Alert",
            launch=Launch(
                name="Starship Flight Test",
                provider="SpaceX",
                launch_time=datetime(2026, 5, 24, 12, 20, tzinfo=UTC),
                source_name="launch-library-2",
                source_launch_id="starship-flight-10",
            ),
            message="unused",
        )

        notification = format_alert(alert, display_timezone="America/Los_Angeles")

        self.assertEqual(notification.title, "Launch Soon: Starship Flight Test")
        self.assertIn("Starship Flight Test", notification.body)
        self.assertIn("Launch Time: 2026-05-24 5:20 AM PDT", notification.body)
        self.assertEqual(
            notification.metadata,
            {
                "alert_kind": "Launch Soon Alert",
                "source_name": "launch-library-2",
                "source_launch_id": "starship-flight-10",
                "launch_name": "Starship Flight Test",
            },
        )

    def test_countdown_and_hour_precision_alerts_have_distinct_soft_wording(
        self,
    ) -> None:
        cases = [
            (
                "Launch Imminent Alert",
                "Launch Imminent: Starship Flight Test",
                "is scheduled now.",
            ),
            (
                "Hour-Precision Alert",
                "Possible Launch This Hour: Starship Flight Test",
                "possible launch this hour.",
            ),
        ]

        for alert_name, expected_title, expected_phrase in cases:
            with self.subTest(alert_name=alert_name):
                notification = format_alert(
                    Alert(
                        name=alert_name,
                        launch=Launch(
                            name="Starship Flight Test",
                            provider="SpaceX",
                            launch_time=datetime(2026, 5, 24, 12, 20, tzinfo=UTC),
                            mission_description="Matched Starship include term",
                        ),
                        message="unused",
                    ),
                    display_timezone="America/Los_Angeles",
                )

                self.assertEqual(notification.title, expected_title)
                self.assertIn(expected_phrase, notification.body)
                self.assertIn(
                    "Launch Time: 2026-05-24 5:20 AM PDT", notification.body
                )
                self.assertNotIn("Matched", notification.body)

    def test_time_change_correction_alert_has_reason_specific_title_and_times(
        self,
    ) -> None:
        notification = format_alert(
            Alert(
                name="Correction Alert",
                launch=Launch(
                    name="Starship Flight Test",
                    provider="SpaceX",
                    launch_time=datetime(2026, 5, 24, 13, 5, tzinfo=UTC),
                    source_name="launch-library-2",
                    source_launch_id="starship-flight-10",
                ),
                message="unused",
                correction_reason="time_changed",
                previous_launch=LaunchSnapshot(
                    name="Starship Flight Test",
                    launch_time=datetime(2026, 5, 24, 12, 20, tzinfo=UTC),
                    timing_precision="precise",
                    launch_status="go",
                ),
            ),
            display_timezone="America/Los_Angeles",
        )

        self.assertEqual(notification.title, "Launch Time Changed: Starship Flight Test")
        self.assertIn(
            "Previous Launch Time: 2026-05-24 5:20 AM PDT", notification.body
        )
        self.assertIn(
            "Current Launch Time: 2026-05-24 6:05 AM PDT", notification.body
        )
        self.assertEqual(notification.metadata["alert_kind"], "Correction Alert")

    def test_status_and_precision_correction_alerts_are_contextual_but_concise(
        self,
    ) -> None:
        cases = [
            (
                "timing_now_imprecise",
                "Launch Time Less Precise: Starship Flight Test",
                "The schedule no longer has a precise Launch Time.",
            ),
            (
                "no_go",
                "Launch No Go: Starship Flight Test",
                "The launch is no longer go.",
            ),
        ]

        for correction_reason, expected_title, expected_phrase in cases:
            with self.subTest(correction_reason=correction_reason):
                notification = format_alert(
                    Alert(
                        name="Correction Alert",
                        launch=Launch(
                            name="Starship Flight Test",
                            provider="SpaceX",
                            launch_time=datetime(2026, 5, 24, 12, 20, tzinfo=UTC),
                            mission_description="Matched Starship include term",
                        ),
                        message="unused",
                        correction_reason=correction_reason,
                        previous_launch=LaunchSnapshot(
                            name="Starship Flight Test",
                            launch_time=datetime(2026, 5, 24, 12, 20, tzinfo=UTC),
                            timing_precision="precise",
                            launch_status="go",
                        ),
                    ),
                    display_timezone="America/Los_Angeles",
                )

                self.assertEqual(notification.title, expected_title)
                self.assertIn(expected_phrase, notification.body)
                self.assertIn(
                    "Launch Time: 2026-05-24 5:20 AM PDT", notification.body
                )
                self.assertNotIn("Matched", notification.body)
                self.assertEqual(
                    notification.metadata["correction_reason"], correction_reason
                )


if __name__ == "__main__":
    unittest.main()
