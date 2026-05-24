from __future__ import annotations

import io
import os
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

from spacex_launch_watcher.cli import build_parser, main


class CliTests(unittest.TestCase):
    def test_config_can_be_provided_before_the_command(self) -> None:
        args = build_parser().parse_args(["--config", "local.toml", "once"])

        self.assertEqual(args.command, "once")
        self.assertEqual(args.config, "local.toml")

    def test_config_can_be_provided_after_the_command(self) -> None:
        args = build_parser().parse_args(["once", "--config", "local.toml"])

        self.assertEqual(args.command, "once")
        self.assertEqual(args.config, "local.toml")

    def test_once_defaults_alert_records_to_root_logs_folder(self) -> None:
        args = build_parser().parse_args(["once"])

        self.assertEqual(args.alert_records, "logs/alert-records.json")

    def test_once_dry_run_prints_candidate_launch_soon_alert(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "config.toml"
            config_path.write_text(
                """
[watcher]
launch_provider = "SpaceX"
include_terms = ["Starship"]

[alert_policy]
launch_soon_minutes_before = 30
launch_imminent_minutes_before = 5

[notification_channel]
type = "pushover"
app_token_env = "PUSHOVER_APP_TOKEN"

[[recipients]]
name = "Nick"
user_key_env = "PUSHOVER_USER_KEY_NICK"
""",
                encoding="utf-8",
            )
            output = io.StringIO()

            with patch.dict(
                os.environ,
                {
                    "PUSHOVER_APP_TOKEN": "unused-in-dry-run",
                    "PUSHOVER_USER_KEY_NICK": "unused-in-dry-run",
                },
            ), redirect_stdout(output):
                exit_code = main(["--config", str(config_path), "once", "--dry-run"])

        self.assertEqual(exit_code, 0)
        text = output.getvalue()
        self.assertIn("Dry Run", text)
        self.assertIn("Launch Soon Alert", text)
        self.assertIn("Starship Flight Test", text)
        self.assertIn("would send", text)
        self.assertIn("Nick", text)
        self.assertNotIn("Notification sent", text)
        self.assertNotIn("Watcher Log written", text)

    def test_once_dry_run_uses_imminent_threshold_for_fake_launch(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "config.toml"
            config_path.write_text(
                """
[watcher]
launch_provider = "SpaceX"
include_terms = ["Starship"]

[alert_policy]
launch_soon_minutes_before = 60
launch_imminent_minutes_before = 50

[notification_channel]
type = "pushover"
app_token_env = "PUSHOVER_APP_TOKEN"

[[recipients]]
name = "Nick"
user_key_env = "PUSHOVER_USER_KEY_NICK"
""",
                encoding="utf-8",
            )
            output = io.StringIO()

            with patch.dict(
                os.environ,
                {
                    "PUSHOVER_APP_TOKEN": "unused-in-dry-run",
                    "PUSHOVER_USER_KEY_NICK": "unused-in-dry-run",
                },
            ), redirect_stdout(output):
                exit_code = main(["--config", str(config_path), "once", "--dry-run"])

        self.assertEqual(exit_code, 0)
        text = output.getvalue()
        self.assertIn("Candidate Alert: Launch Imminent Alert", text)
        self.assertNotIn("Candidate Alert: Launch Soon Alert", text)

    def test_once_writes_alert_records_before_fake_notification_delivery(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "config.toml"
            records_path = Path(temp_dir) / "alert-records.json"
            config_path.write_text(
                """
[watcher]
launch_provider = "SpaceX"
include_terms = ["Starship"]

[notification_channel]
type = "pushover"
app_token_env = "PUSHOVER_APP_TOKEN"

[[recipients]]
name = "Nick"
user_key_env = "PUSHOVER_USER_KEY_NICK"
""",
                encoding="utf-8",
            )
            output = io.StringIO()

            with patch.dict(
                os.environ,
                {
                    "PUSHOVER_APP_TOKEN": "unused-fake-channel",
                    "PUSHOVER_USER_KEY_NICK": "unused-fake-channel",
                },
            ), redirect_stdout(output):
                exit_code = main(
                    [
                        "--config",
                        str(config_path),
                        "once",
                        "--alert-records",
                        str(records_path),
                    ]
                )

            self.assertEqual(exit_code, 0)
            self.assertTrue(records_path.exists())
            text = output.getvalue()
            self.assertIn("Created 1 Alert(s).", text)
            self.assertIn("Delivered 1 fake Notification(s).", text)


if __name__ == "__main__":
    unittest.main()
