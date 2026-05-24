# Rocket Launch Notifier

Personal SpaceX launch watcher that sends Pushover notifications when selected launches are close enough to go open the live feed.

## V1 Shape

- Python watcher service
- Dockerized for Raspberry Pi or another always-on host
- Launch Library 2 as the first schedule source
- Pushover notifications to one or more recipients
- Pacific-time notification text
- Minimal runtime output in a bind-mounted `logs/` directory

## Relevant Launches

V1 watches SpaceX launches that match one of these configured terms:

- Vandenberg
- Starbase
- Boca Chica
- Starship

## Planned Commands

```bash
python -m spacex_launch_watcher watch
python -m spacex_launch_watcher once
python -m spacex_launch_watcher test-notification
```

See `CONTEXT.md` and `docs/adr/` for the current product and architecture decisions.
