# ADR 0001: Build a Containerized Python Watcher

## Status

Accepted

## Context

The project needs reliable personal notifications for selected SpaceX launches. A polished web or mobile app is not needed for the first version. The intended first runtime is a Raspberry Pi, with the option to move to another host later.

## Decision

Build v1 as a small Python watcher service packaged with Docker.

The service should keep runtime output in a bind-mounted `logs/` directory and read secrets from `.env`.

The service should provide three commands:

- `watch`
- `once`
- `test-notification`

## Consequences

The implementation can stay small and testable. Moving between hosts should be simple because runtime-specific details live in `.env` and `logs/`.

The project should avoid native mobile app work and broad UI work until the watcher has proven useful.
