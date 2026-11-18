RocketLaunchNotifier — a C++20 app that polls the Launch Library 2 API and sends SpaceX launch alerts via Pushover; built, tested, and run via CMake and Docker.

Deployed on a Raspberry Pi (`pop@pi-server.local`, aarch64). Access it with `ssh pi` (alias in `~/.ssh/config`, key-based, no password); use `-o BatchMode=yes` for non-interactive commands. Never read the private key `~/.ssh/pi_claude`.

