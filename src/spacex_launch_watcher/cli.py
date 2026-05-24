from __future__ import annotations

import argparse
from pathlib import Path

from spacex_launch_watcher.config import ConfigError, load_config
from spacex_launch_watcher.watcher import run_dry_watcher_cycle


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
        if command == "once":
            command_parser.add_argument(
                "--dry-run",
                action="store_true",
                help="Run one Watcher evaluation with fake launch data.",
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
        print(run_dry_watcher_cycle(config))
    else:
        print(
            f"{args.command}: configuration valid for "
            f"{config.watcher.launch_provider} with {len(config.recipients)} recipient(s)."
        )
    return 0
