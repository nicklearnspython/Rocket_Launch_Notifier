from __future__ import annotations

import unittest

from spacex_launch_watcher.config import (
    DEFAULT_CORRECTION_THRESHOLD_MINUTES,
    DEFAULT_DISPLAY_TIMEZONE,
    DEFAULT_HOUR_PRECISION_WINDOW_MINUTES,
    DEFAULT_LAUNCH_IMMINENT_MINUTES,
    DEFAULT_LAUNCH_SOON_MINUTES,
    DEFAULT_POLL_INTERVAL_SECONDS,
    DEFAULT_RETENTION_DAYS,
    DEFAULT_SCHEDULE_LOOKAHEAD_HOURS,
    DEFAULT_SOURCE_NAME,
    ConfigError,
    parse_config,
)


def valid_config() -> dict:
    return {
        "watcher": {
            "launch_provider": "SpaceX",
            "include_terms": ["Starship"],
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


def valid_env() -> dict[str, str]:
    return {
        "PUSHOVER_APP_TOKEN": "app-token",
        "PUSHOVER_USER_KEY_NICK": "user-key",
    }


class ConfigTests(unittest.TestCase):
    def test_config_defaults_are_applied(self) -> None:
        config = parse_config(valid_config(), valid_env())

        self.assertEqual(config.watcher.display_timezone, DEFAULT_DISPLAY_TIMEZONE)
        self.assertEqual(
            config.watcher.poll_interval_seconds, DEFAULT_POLL_INTERVAL_SECONDS
        )
        self.assertEqual(config.watcher.retention_days, DEFAULT_RETENTION_DAYS)
        self.assertEqual(
            config.watcher.schedule_lookahead_hours, DEFAULT_SCHEDULE_LOOKAHEAD_HOURS
        )
        self.assertEqual(config.launch_schedule_source.name, DEFAULT_SOURCE_NAME)
        self.assertEqual(
            config.alert_policy.launch_soon_minutes_before,
            DEFAULT_LAUNCH_SOON_MINUTES,
        )
        self.assertEqual(
            config.alert_policy.launch_imminent_minutes_before,
            DEFAULT_LAUNCH_IMMINENT_MINUTES,
        )
        self.assertEqual(
            config.alert_policy.hour_precision_window_minutes,
            DEFAULT_HOUR_PRECISION_WINDOW_MINUTES,
        )
        self.assertEqual(
            config.alert_policy.correction_threshold_minutes,
            DEFAULT_CORRECTION_THRESHOLD_MINUTES,
        )
        self.assertEqual(config.notification_channel.app_token, "app-token")
        self.assertEqual(config.recipients[0].user_key, "user-key")

    def test_validation_failures(self) -> None:
        cases = [
            (
                lambda config: config["watcher"].pop("launch_provider"),
                "missing Launch Provider",
            ),
            (
                lambda config: config["watcher"].pop("include_terms"),
                "missing Include Terms",
            ),
            (lambda config: config.pop("recipients"), "missing recipients"),
            (
                lambda config: config["watcher"].update(
                    {"display_timezone": "Mars/Olympus_Mons"}
                ),
                "invalid display timezone",
            ),
            (
                lambda config: config.setdefault("alert_policy", {}).update(
                    {
                        "launch_soon_minutes_before": 5,
                        "launch_imminent_minutes_before": 30,
                    }
                ),
                "alert policy must make launch_soon_minutes_before greater",
            ),
        ]

        for mutate, expected_message in cases:
            with self.subTest(expected_message=expected_message):
                raw_config = valid_config()
                mutate(raw_config)

                with self.assertRaisesRegex(ConfigError, expected_message):
                    parse_config(raw_config, valid_env())

    def test_missing_referenced_secret_environment_variables_fail_clearly(
        self,
    ) -> None:
        with self.assertRaises(ConfigError) as error:
            parse_config(valid_config(), {})

        message = str(error.exception)
        self.assertIn(
            "missing referenced secret environment variable: PUSHOVER_APP_TOKEN",
            message,
        )
        self.assertIn(
            "missing referenced secret environment variable: PUSHOVER_USER_KEY_NICK",
            message,
        )


if __name__ == "__main__":
    unittest.main()
