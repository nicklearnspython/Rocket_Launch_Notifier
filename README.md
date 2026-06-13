# Rocket Launch Notifier

A small personal SpaceX launch watcher, written in C++20. It polls
[Launch Library 2](https://thespacedevs.com/llapi), decides when a relevant
launch is close enough to care about, and sends conservative alerts through
[Pushover](https://pushover.net/).

Everything builds and runs inside Docker, so the same image works on a
desktop and a Raspberry Pi.

## Architecture

A pure decision core surrounded by adapters, with SOLID-style interfaces at
each boundary (`LaunchScheduleSource`, `NotificationChannel`,
`AlertRecordStore`). Headers live in [include/rocket_watcher/](include/rocket_watcher/),
implementations in [src/](src/):

- [domain.hpp](include/rocket_watcher/domain.hpp) — project-level model: `Launch`,
  timing precision (`precise`/`hour`/`imprecise`/`unknown`), launch status
  (`go`/`no_go`/`ended`/`unknown`), `Alert`, `AlertRecord`.
- [relevance.hpp](include/rocket_watcher/relevance.hpp) — case-insensitive provider +
  include-term matching across searchable fields.
- [core.hpp](include/rocket_watcher/core.hpp) — the pure decision core. Consumes
  normalized launches, prior alert records, policy, and the current time;
  returns semantic alerts plus the updated records. No IO.
- [storage.hpp](include/rocket_watcher/storage.hpp) — alert records in one JSON file,
  written atomically, UTC timestamps, ~30-day retention.
- [formatting.hpp](include/rocket_watcher/formatting.hpp) — turns alerts into
  channel-independent messages (titles, lean bodies, display-timezone times).
- [launch_library.hpp](include/rocket_watcher/launch_library.hpp) — maps LL2 payloads
  into the source contract; unmapped values become `unknown` plus a
  human-readable warning.
- [pushover.hpp](include/rocket_watcher/pushover.hpp) — delivery only; receives
  formatted content, never Launch/Alert objects.
- [orchestration.hpp](include/rocket_watcher/orchestration.hpp) — the single-cycle
  function shared by `once` and `watch`: fetch → decide → save records → notify.
  If records can't be saved, nothing is sent; a failed notification never rolls
  back a saved record.
- [http.hpp](include/rocket_watcher/http.hpp) — HTTP seams; libcurl in production,
  fakes in tests.
- [cli.hpp](include/rocket_watcher/cli.hpp) — `once`, `watch`, `test-notification`
  commands with injectable factories for testing.

Third-party libraries (fetched automatically by CMake): nlohmann/json,
toml++, GoogleTest/GoogleMock. libcurl comes from the system/base image.

## Alert behavior

- **Launch Soon / Launch Imminent** — for go launches with precise timing,
  within the configured thresholds. Only the most urgent unsent alert fires.
- **Possible launch this hour** — softer alert when timing is only precise to
  the hour. Doesn't suppress later precise alerts.
- **Corrections** — only after a prior alert, with reason-specific titles:
  no longer go, time changed (≥ configurable shift, default 30 min), timing
  now imprecise.
- Imprecise/unknown timing and ended/unknown status are silent.

## Setup

```sh
copy config.example.toml config.toml  # then edit include terms etc.
copy .env.example .env                # then add real Pushover secrets
```

## Running (Docker)

```sh
docker compose up -d --build          # long-running watch loop
```

Mounts `config.toml` read-only, persists `data/` (alert records) and `logs/`,
and reads secrets from `.env`. One-off commands:

PowerShell (Windows):

```powershell
docker build --target runtime -t rocket-watcher .
docker run --rm -v "${PWD}\config.toml:/app/config.toml:ro" rocket-watcher once --dry-run
docker run --rm --env-file .env -v "${PWD}\config.toml:/app/config.toml:ro" rocket-watcher test-notification
```

Linux / Raspberry Pi:

```sh
docker build --target runtime -t rocket-watcher .
docker run --rm -v "$(pwd)/config.toml:/app/config.toml:ro" rocket-watcher once --dry-run
docker run --rm --env-file .env -v "$(pwd)/config.toml:/app/config.toml:ro" rocket-watcher test-notification
```

The host side of `-v` must be an absolute path; a bare name like
`config.toml` makes Docker silently create an empty named volume instead.

Commands: `once [--dry-run]`, `watch`, `test-notification`. Exit codes:
`once`/`test-notification` exit nonzero if the source fails or any recipient
delivery fails; config/secret problems exit 2. `watch` logs failures and
keeps polling.

## Tests and coverage

The `test` image stage compiles with GCC 14, runs the full GTest/GMock suite,
and fails the build if line coverage drops below 60% (currently ~84%):

```sh
docker build --target test --progress=plain .
```

No network access needed by the tests; LL2 and Pushover are faked through the
HTTP seams.

## Building outside Docker

Needs CMake ≥ 3.24, a C++20 compiler with `std::chrono` time-zone support
(GCC 13+), and libcurl dev headers:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Lint / format

Style is enforced by [.clang-format](.clang-format) (Google style, 2-space
indent) and [.clang-tidy](.clang-tidy):

```sh
clang-format -i src/*.cpp include/rocket_watcher/*.hpp tests/*.cpp tests/*.hpp
cmake -S . -B build -DROCKET_WATCHER_CLANG_TIDY=ON   # tidy runs during compile
```

## Raspberry Pi

The Dockerfile is architecture-independent; on the Pi (64-bit Raspberry Pi
OS with Docker installed) the same `docker compose up -d --build` builds an
arm64 image and runs it.
