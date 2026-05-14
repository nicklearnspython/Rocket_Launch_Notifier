# Rocket Launch Notifier Context

## Purpose

Rocket Launch Notifier is a personal SpaceX launch watcher. It should notify configured people when a relevant launch is close enough that they may want to stop what they are doing and open the live feed.

The first version should be a small, testable watcher service, not a full app.

## Relevant Launches

A launch is relevant when the provider is SpaceX and at least one searchable launch field matches one of the configured include terms.

Initial include terms:

- `vandenberg`
- `starbase`
- `boca chica`
- `starship`

The broad Starship match is intentional. Future launch site naming may change, and the watcher should be easy to adjust with configuration.

## Notifications

Pushover is the v1 notification channel.

The watcher should support multiple Pushover user keys so the same alerts can go to the owner and the owner's dad. Recipient configuration is private and must stay out of git.

All configured recipients receive the same alerts:

- T-30 countdown alert
- T-5 countdown alert
- hour-precision launch window heads-up
- correction after any prior alert when time, status, or timing precision changes materially

Notifications should use local Pacific time via `America/Los_Angeles`.

Notifications should not include livestream links in v1.

## Alert Rules

Exact countdown alerts are for concrete launch times:

- T-30 fires once when launch is between 30 and 20 minutes away.
- T-5 fires once when launch is between 5 minutes and liftoff.
- Countdown alerts require a concrete `net` time and precise timing, expected to mean `net_precision` of minute or second.

Hour-precision launches get a softer heads-up:

- Fire once when the launch window appears to be within the current hour.
- Wording should make clear that the live feed is the source of truth for final timing.

The watcher should stay silent for vague planning dates such as day, month, year, TBD, or "NET May".

If any alert was already sent for a launch, the watcher should send a correction when:

- the launch time changes
- the status changes away from Go
- timing precision degrades
- the launch disappears from relevant upcoming results

If the data source is unreachable, rate-limited, or malformed, the watcher should log the error, keep existing state, send no notifications, and retry on the next poll.

## Runtime

The preferred runtime is Docker on a Raspberry Pi or similar always-on machine.

State should be stored in a bind-mounted data directory so the service can move between hosts by copying `.env` and `data/`.

The watcher should expose at least these commands:

- `watch`: run the polling loop
- `once`: fetch, evaluate, notify if needed, save state, and exit
- `test-notification`: send an explicit Pushover test notification

No automatic startup notification should be sent.

## State

State exists to make notification behavior correct. It is not intended to be a human-facing history ledger.

Minimal state should track, per launch:

- last seen launch time
- last seen status
- last seen precision
- alerts already sent

The watcher may keep a small post-`net` grace window, initially 10 minutes, for status and correction checks. It should not send new countdown or hour-window alerts after `net`.
