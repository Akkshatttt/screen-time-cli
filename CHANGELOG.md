# Changelog

Semantic versioning, still 0.x: the minor number grows with features and the patch number with fixes. 1.0.0 is for when the core planned features exist.

## 0.2.1 (2026-11-09)
- Safe upgrades: `install` over an existing install stages `*.new.exe`, verifies the new CLI runs, swaps with the previous files kept as `*.exe.old`, requires the new daemon to start, and rolls back to the old files (and restarts the old daemon) on any failure. Registry entries are written last.
- Database schema versioning (`PRAGMA user_version`, ordered additive migrations, `VACUUM INTO` backup `data.db.bak-v<N>` before migrating existing data). A database newer than the program is refused with a clear message instead of being misread.
- Daemon records its version; `status` and `doctor` warn when daemon and CLI differ; `doctor` reports leftovers and `--fix` cleans them.
- `check-update`: on demand only, asks the GitHub releases API for a newer version; never downloads or installs.
- Fix: data file name was built with a bad escape in the new code path; caught by the migration test before release.

## 0.2.0 (2026-11-09)
- `install` / `uninstall [--purge]` / `doctor [--fix]`: per-user install in `%LOCALAPPDATA%\Programs\screen-time-cli`, Settings > Apps entry, autostart via HKCU Run, user PATH. No admin. Idempotent; `doctor --fix` restores entries a cleaner removed.
- `version` command, `README.md`, registry/install tests against a throw-away registry subtree.

## 0.1.0 (2026-11-09)
- Tracker daemon: SetWinEventHook foreground and title events, exe + window title, active / passive / away states (passive = fullscreen or foreground-app audio, away after 10 min or lock/sleep), 30 s heartbeat.
- SQLite store (WAL) with spans; CLI: `status`, `today`, `week`, `month`, `start`, `stop`, `pause`, `resume`.
- `make test` core tests, `make bench` resource bench with budgets.
- Not yet: install/autostart (0.2), charts (0.3), insights (0.4).
