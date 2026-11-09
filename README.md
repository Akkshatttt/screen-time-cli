# screen-time-cli

Tiny terminal-only screen-time tracker for Windows. A hidden background daemon records the foreground app and window
title (event driven, near-zero CPU) into a local SQLite file; the CLI reads it. Nothing leaves the machine.

## Build
MinGW gcc: `mingw32-make all test`. Produces `build/screen-time-cli.exe` and `build/screen-time-daemon.exe`.

## Install (per user, no admin)
`build\screen-time-cli.exe install` copies both exes to `%LOCALAPPDATA%\Programs\screen-time-cli`, adds it to your user
PATH, starts the daemon at logon, and lists the app in Settings > Apps. `screen-time-cli uninstall [--purge]` removes it.

## Commands
| Command | Does |
|---|---|
| `status` | daemon state and last recorded window |
| `today` / `week` / `month` | per-app time (active and passive) |
| `start` / `stop` / `pause` / `resume` | control the daemon |
| `install` / `uninstall [--purge]` | per-user install and removal |
| `doctor [--fix]` | check the installation, optionally repair it |
| `version` | print version |

Time is split into active (recent input), passive (no input but fullscreen or audio playing) and away (idle 10 min,
locked, asleep; not counted). Optional `config.ini` in the data folder: `[tracking]` `active_seconds`, `away_seconds`,
`heartbeat_seconds`.
