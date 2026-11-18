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
- [digest.hpp](include/rocket_watcher/digest.hpp) — Weekly Digest schedule, launch
  selection, and the small file remembering which week's digest was sent.
- [http.hpp](include/rocket_watcher/http.hpp) — HTTP seams; libcurl in production,
  fakes in tests.
- [cli.hpp](include/rocket_watcher/cli.hpp) — `once`, `watch`, `digest`,
  `test-notification` commands with injectable factories for testing.

Third-party libraries (fetched automatically by CMake): nlohmann/json,
toml++, GoogleTest/GoogleMock. libcurl comes from the system/base image.

## Alert behavior

- **Launch Soon / Launch Imminent** — for go launches with precise timing,
  within the configured thresholds. Only the most urgent unsent alert fires.
  After any correction (e.g. a scrub moves the launch time) the countdown
  resets, so the next attempt gets its own Soon/Imminent alerts.
- **Possible launch this hour** — softer alert when timing is only precise to
  the hour. Doesn't suppress later precise alerts.
- **Corrections** — only after a prior alert, with reason-specific titles:
  no longer go, time changed (≥ configurable shift, default 30 min), timing
  now imprecise.
- Imprecise/unknown timing and ended/unknown status are silent.

Notifications are titled with the mission (e.g. `Launch soon: Starlink
Group 17-44`) and the body opens with the vehicle and pad (e.g. `Falcon 9
from SLC-4E, Vandenberg SFB, CA`).

## Weekly Digest

Once a week (default Sunday 20:00 in `display_timezone`) every recipient gets
a short heads-up of the tracked launches in the next 7 days, for example:

```
This week: 2 tracked launches

Starlink Group 11-4
Tue Sep 29, 15:30 PDT · go
Falcon 9 from SLC-4E, Vandenberg SFB, CA

Flight 12
Thu Oct 1 (date tentative) · not yet confirmed
Starship from Orbital Launch Mount A, Starbase, TX

Times often slip; countdown alerts will follow.
```

It is awareness only: no follow-ups when those launches move. A quiet week
still gets a one-line "no tracked launches" note. The digest reuses the
normal poll's fetch; the sent week is remembered in `data/digest_state.json`
so restarts don't repeat it, and a digest missed by more than 3 hours (e.g.
the Pi was off) is skipped until the next week. Configure it under
`[digest]`; `digest --dry-run` prints this week's digest without sending.

## Logging

Each line goes to stdout and `logs/watcher.log`. By default the log is quiet:

- always: alerts, deliveries, fetch/storage failures, new source warnings
- tracked (relevant) launches: one line when first seen, when their state
  changes, and when they leave the schedule
- hourly: `status: 50 upcoming launches evaluated, 14 tracked; 12 check(s)
  since last status, 0 failed`

Set `verbose = true` under `[watcher]` to also log every launch evaluation
each cycle; `once --dry-run` always does. The log rotates to `watcher.log.1`
at `log_max_size_mb` (default 5), and `docker-compose.yml` caps Docker's own
copy of stdout the same way.

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

Commands: `once [--dry-run]`, `watch`, `digest [--dry-run]`,
`test-notification`. `digest` sends the Weekly Digest now (off schedule; the
scheduled one still goes out). Exit codes:
`once`/`digest`/`test-notification` exit nonzero if the source fails or any recipient
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

## Dev container

[.devcontainer/](.devcontainer/devcontainer.json) opens the project in the
Dockerfile's `dev` stage: GCC 14, CMake, Ninja, libcurl, gcovr, clang-format,
clang-tidy, and gdb. It is the same toolchain the `test` stage builds on, so
"Reopen in Container" in VS Code matches what CI-style test builds see.

## Deploying to the Raspberry Pi

Develop and build here; the Pi only runs the finished image. With Docker
Desktop running and the `pi` ssh alias set up:

```sh
scripts/deploy.sh
```

It runs the test stage, builds the arm64 `runtime` image with buildx (QEMU
emulation, so it is slow), streams it to the Pi with `docker save | ssh
docker load`, copies `docker-compose.yml`, and restarts with
`docker compose up -d --no-build`. The Pi has under 1 GB of RAM; compiling
there (especially the test stage) can exhaust memory, so avoid it.
`PI_HOST` and `PI_DIR` override the target host and project directory.
