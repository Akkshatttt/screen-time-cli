# Changelog

Semantic versioning, still 0.x: the minor number grows with features and the patch number with fixes. 1.0.0 is for when the core planned features exist.

## 0.2.0 (2026-11-09)
- `install` / `uninstall [--purge]` / `doctor [--fix]`: per-user install in `%LOCALAPPDATA%\Programs\screen-time-cli`, Settings > Apps entry, autostart via HKCU Run, user PATH. No admin. Idempotent; `doctor --fix` restores entries a cleaner removed.
- `version` command, `README.md`, registry/install tests against a throw-away registry subtree.

## 0.1.0 (2026-11-09)
- Tracker daemon: SetWinEventHook foreground and title events, exe + window title, active / passive / away states (passive = fullscreen or foreground-app audio, away after 10 min or lock/sleep), 30 s heartbeat.
- SQLite store (WAL) with spans; CLI: `status`, `today`, `week`, `month`, `start`, `stop`, `pause`, `resume`.
- `make test` core tests, `make bench` resource bench with budgets.
- Not yet: install/autostart (0.2), charts (0.3), insights (0.4).
