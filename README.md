# screen-time-cli

Tiny terminal-only screen-time tracker for Windows. A hidden background daemon records the foreground app and window
title (event driven, near-zero CPU) into a local SQLite file; the CLI reads it. Nothing leaves the machine.

<p align="center"><img src="assets/icons/png/screen-time-cli-128.png" alt="screen-time-cli icon" width="96"></p>

## Build
MinGW gcc: `mingw32-make all test`. Produces `build/screen-time-cli.exe` and `build/screen-time-daemon.exe`.

## Install (per user, no admin)
`build\screen-time-cli.exe install` copies both exes to `%LOCALAPPDATA%\Programs\screen-time-cli`, adds it to your user
PATH, starts the daemon at logon, and lists the app in Settings > Apps. `screen-time-cli uninstall` removes the application and keeps your data; `uninstall --clean` removes everything.

## Upgrading
Build or download the new version, then run `install` from it. The old files are kept until the new daemon is confirmed
running and restored automatically if anything fails. Your data lives in `%LOCALAPPDATA%\screen-time-cli` and is never
touched by upgrades. Databases are migrated in place after a backup (`data.db.bak-v<N>`).

## Commands
| Command | Does |
|---|---|
| `status` | daemon state and last recorded window |
| `report` (`today` / `week` / `month`) | per-app time with bars and trends. Options: `--days N`, `--app NAME`, `--by category`, `--top N`, `--json` |
| `heatmap` | weekday x hour-of-day usage grid |
| `trends` | this period vs the previous one, weekly rhythm, late-night share |
| `focus` | sessions, breaks, longest single-app blocks, focus score |
| `switches` | app switching rate, by hour, most common hops |
| `topics` | time by topic from window titles |
| `live [view]` | full-screen window; each view replaces the last and refreshes every 5 s. Keys: `t` today, `r` report, `w` week, `m` month, `h` heatmap, `e` trends, `f` focus, `s` switches, `o` topics, space refresh, `:` command line (e.g. `:report --days 14 --app chrome`), `q` quit. Your shell screen is restored on exit |
| `start` / `stop` / `pause` / `resume` | control the daemon |
| `install` | per-user install or in-place upgrade |
| `uninstall` | remove the application, keep your data (asks y/N; `--yes` skips) |
| `uninstall --clean` | also delete all data: database, backups, `config.ini` |
| `doctor [--fix]` | check the installation, optionally repair it |
| `check-update` | ask GitHub whether a newer release exists (never installs) |
| `version` | print version |

Output is colored with Unicode bars in a console and plain ASCII when piped; set `NO_COLOR` to turn color off. Every
report takes `--json` for scripts. Categories and topics come from `config.ini` in the data folder, for example:

    [categories]
    Work=Code.exe,WINWORD.EXE
    [topics]
    YouTube=youtube
    Mail=gmail|outlook

Time is split into active (recent input), passive (no input but fullscreen or audio playing) and away (idle 10 min,
locked, asleep; not counted). Optional `config.ini` in the data folder: `[tracking]` `active_seconds`, `away_seconds`,
`heartbeat_seconds`.
