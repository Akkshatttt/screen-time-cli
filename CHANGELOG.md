# Changelog

Semantic versioning, still 0.x: the minor number grows with features and the patch number with fixes. 1.0.0 is for when the core planned features exist.

## 0.1.0 (2026-11-09)
- Tracker daemon: SetWinEventHook foreground and title events, exe + window title, active / passive / away states (passive = fullscreen or foreground-app audio, away after 10 min or lock/sleep), 30 s heartbeat.
- SQLite store (WAL) with spans; CLI: `status`, `today`, `week`, `month`, `start`, `stop`, `pause`, `resume`.
- `make test` core tests, `make bench` resource bench with budgets.
- Not yet: install/autostart (0.2), charts (0.3), insights (0.4).
