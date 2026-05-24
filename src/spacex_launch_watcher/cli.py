from __future__ import annotations

import argparse
from pathlib import Path

from spacex_launch_watcher.config import ConfigError, load_config
from spacex_launch_watcher.watcher import (
    DeliveryFailure,
    FakeNotificationChannel,
    SourceFailure,
    run_dry_watcher_cycle,
    run_watch,
    run_watcher_cycle,
)

DEFAULT_ALERT_RECORDS_PATH = "logs/alert-records.json"
DEFAULT_WATCHER_LOG_PATH = "logs/watcher.log"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="spacex_launch_watcher")
    _add_config_argument(parser, default="config.example.toml")

    subparsers = parser.add_subparsers(dest="command", required=True)
    for command, help_text in [
        ("watch", "Run the Watcher continuously."),
        ("once", "Run one Watcher evaluation."),
        ("test-notification", "Validate notification settings."),
    ]:
        command_parser = subparsers.add_parser(command, help=help_text)
        _add_config_argument(command_parser, default=argparse.SUPPRESS)
        if command in {"once", "watch"}:
            command_parser.add_argument(
                "--alert-records",
                default=DEFAULT_ALERT_RECORDS_PATH,
                help="Path to the Alert Records JSON file.",
            )
            command_parser.add_argument(
                "--watcher-log",
                default=DEFAULT_WATCHER_LOG_PATH,
                help="Path to the human-readable Watcher Log file.",
            )
        if command == "once":
            command_parser.add_argument(
                "--dry-run",
                action="store_true",
                help="Run one Watcher evaluation with fake launch data.",
            )
        if command == "watch":
            command_parser.add_argument(
                "--max-polls",
                type=int,
                default=None,
                help=argparse.SUPPRESS,
            )
    return parser


def _add_config_argument(parser: argparse.ArgumentParser, default: object) -> None:
    parser.add_argument(
        "--config",
        default=default,
        help="Path to the non-secret Watcher behavior config TOML file.",
    )


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        config = load_config(Path(args.config))
    except ConfigError as error:
        parser.exit(2, f"{error}\n")

    if args.command == "once" and args.dry_run:
        try:
            print(run_dry_watcher_cycle(config))
        except SourceFailure as error:
            print(f"Launch Schedule Source failure: {error}")
            return 1
    elif args.command == "once":
        notification_channel = FakeNotificationChannel()
        try:
            decision = run_watcher_cycle(
                config=config,
                alert_records_path=Path(args.alert_records),
                watcher_log_path=Path(args.watcher_log),
                notification_channel=notification_channel,
            )
        except SourceFailure as error:
            print(f"Launch Schedule Source failure: {error}")
            return 1
        except DeliveryFailure as error:
            print(error)
            return 1
        except OSError as error:
            print(f"Watcher failure: {error}")
            return 1
        print(f"Created {len(decision.alerts)} Alert(s).")
        print(f"Delivered {len(notification_channel.deliveries)} fake Notification(s).")
    elif args.command == "watch":
        run_watch(
            config=config,
            alert_records_path=Path(args.alert_records),
            watcher_log_path=Path(args.watcher_log),
            max_polls=args.max_polls,
        )
    else:
        print(
            f"{args.command}: configuration valid for "
            f"{config.watcher.launch_provider} with {len(config.recipients)} recipient(s)."
        )
    return 0
