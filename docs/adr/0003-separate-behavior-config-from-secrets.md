# ADR 0003: Separate Behavior Config From Secrets

## Status

Accepted

## Context

The watcher needs editable behavior configuration, including include terms, recipients, alert timing thresholds, and retention settings. It also needs secrets for the notification channel.

Putting all configuration in environment variables would keep deployment simple, but it would make recipient and alert policy changes harder to inspect and edit. Putting secrets in a config file would make local editing convenient, but would increase the risk of committing sensitive values.

## Decision

Use a small checked-in example config file for non-secret watcher behavior, and use `.env` for secrets.

The config file should contain values such as include terms, recipient names, notification channel selection, alert policy thresholds, and references to secret environment variable names.

The `.env` file should contain actual notification channel secrets, such as Pushover application tokens and recipient user keys.

## Consequences

Behavior changes can be reviewed and edited without touching code.

Secrets remain outside the repository and can vary by host.

The watcher needs startup validation that reports missing config values or missing referenced environment variables clearly.
