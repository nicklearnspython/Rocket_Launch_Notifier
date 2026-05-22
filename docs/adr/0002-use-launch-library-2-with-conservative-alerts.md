# ADR 0002: Use Launch Library 2 With Conservative Alerts

## Status

Accepted

## Context

SpaceX launch timing changes frequently. Starship timing is especially volatile. The watcher should help the user know when to open the live feed, but it should not pretend that a schedule feed is more authoritative than it is.

The Space Devs Launch Library 2 is the first planned schedule source. Public/free access has rate limits, so v1 should avoid aggressive final-hour polling.

## Decision

Use Launch Library 2 as the initial launch schedule source.

Use conservative countdown alerts:

- Send T-30 and T-5 only for relevant SpaceX launches with concrete minute-or-second precision timing.
- Send a softer hour-window heads-up when timing precision is hour-level and the launch is within the current hour.
- Stay silent for day, month, year, TBD, or otherwise vague dates.
- After any prior alert for a launch, send corrections for material time, status, or precision changes.
- Treat a correction as material when the launch status changes from go to no go, timing precision changes from precise to imprecise, or the launch time moves enough that a recipient who already got a countdown alert would likely need to re-check the live feed.
- Do not send correction alerts for ended or unknown launch statuses in v1.
- Send no notification when the watcher cannot reach or parse the data source.

## Consequences

The watcher favors useful, honest alerts over exhaustive launch schedule awareness.

The T-5 notification may arrive slightly late under the free API limit and a 5-minute polling interval. That is acceptable for v1 because the T-30 or hour-window alert is the main prompt to open the live source of truth.
