from __future__ import annotations

import unittest

from spacex_launch_watcher.cli import build_parser


class CliTests(unittest.TestCase):
    def test_config_can_be_provided_before_the_command(self) -> None:
        args = build_parser().parse_args(["--config", "local.toml", "once"])

        self.assertEqual(args.command, "once")
        self.assertEqual(args.config, "local.toml")

    def test_config_can_be_provided_after_the_command(self) -> None:
        args = build_parser().parse_args(["once", "--config", "local.toml"])

        self.assertEqual(args.command, "once")
        self.assertEqual(args.config, "local.toml")


if __name__ == "__main__":
    unittest.main()
