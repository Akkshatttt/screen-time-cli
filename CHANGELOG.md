# Changelog

Semantic versioning, still 0.x: the minor number grows with features and the patch number with fixes. 1.0.0 is for when the core planned features exist.

## 0.5.0 (2026-11-13)
- `today` and `report` are now different views. `today` is a one-screen glance (pace against yesterday at the same time of day, what is running now, hourly strip by category, top 5 apps, sessions and switches). `report` is a dashboard: plain-language findings, scores (focus, fragmentation, variety, consistency), category mix with trends, a category x hour rhythm grid, weekend share, focus per category (average and longest stretch, deep share, switches per active hour), unusual days and apps. Its default is 7 days, shortened when less data exists. `week` and `month` keep the old per-app bars view.
- New `label` command: give an exe a friendly name and a category or topic (numbered picker, or `label <exe> [name] [--category C] [--topic T]`, `label --clear <exe>`). Stored in a new `labels.ini` that the program owns; `config.ini` is still never written. Labels show everywhere an app name is shown, `--app` matches labels, and an exe assigned to a topic now counts under that topic in `topics`.
- New `export [spans|daily] [--format csv|json] [--out FILE]`; refuses to overwrite a file.
- `live`: views taller than the window scroll (arrows, PgUp/PgDn, Home/End) and the footer shows the line range, so the Daily and Hour of day charts no longer get cut off.
- `--json` only gains fields: `report` adds `scores`, `categories`, `findings`, `anomalies` and a `label` per item; `today` is a new command object. No schema change (still v1), no new dependency. Cost: `report` runs five scans instead of two (7 days about 160 ms, one year about 0.7 s on 5 years of synthetic data on this machine).

## 0.4.0 (2026-11-09)
- New `live` command: a full-screen window on the terminal's alternate screen. Hotkeys (t, r, w, m, h, e, f, s, o) or a `:` command line switch views; each view replaces the previous one in place and refreshes every 5 s; `q` or Ctrl+C returns to the shell exactly as it was. Needs a console (refuses when piped). One-shot commands and `--json` output are unchanged. No schema change, no new dependency (about 150 lines in `src/cli/live.c`).

## 0.3.2 (2026-11-09)
- Reverted two 0.3.1 changes that were not needed yet: the title-event backoff (the daemon listens for title changes again at all times; titles are recorded after 5 s of quiet as before) and the trimmed SQLite build (the daemon links the same SQLite as the CLI; exe 583 -> 737 KB). Measured effect on the daemon: none on memory (working set 0.6 MB, private working set 0.18 MB, private bytes 1.3 MB) or CPU.
- App icon: the supplied artwork is now built into 10 sizes (16 to 256 px, rounded transparent corners, tighter crop for small sizes) under `assets/icons/` (`tools/make_icons.py`), and embedded with version info in both exes, so Task Manager, Settings > Apps and Explorer show it, and Task Manager names the daemon "screen-time-cli background tracker". The single version source is now `src/core/version.h`. Exes grow by about 110 KB each.
- Kept: working-set trim, lazy audio libraries, heartbeat file, one-transaction changes, prepared statements, single armed title timer, probe cadence.

## 0.3.1 (2026-11-09)
- Daemon memory cut: working set 9.3 MB -> about 0.6 MB, private working set 1.0 MB -> about 0.2 MB (measured on the same machine). Shared Windows DLLs no longer sit in its working set (periodic trim), ole32/COM and the audio DLLs are loaded only for the duration of an audio probe, the text-services framework is not loaded, SQLite lookaside is off and its cache is 64 KB, and the daemon links a trimmed SQLite (exe 732 -> 583 KB).
- Heartbeats no longer write to the database: the daemon overwrites a 24-byte `hb.bin` once per beat; reports and `status` overlay it on the open span, and a restart applies it after a crash. Default heartbeat is now 60 s.
- Animated window titles: name-change events are counted, and the daemon stops listening for 30 s, doubling to 8 min while the storm continues. Per-event timer calls removed. Exe, title and span rows share one transaction per change; statements are prepared once; WAL checkpoints every 128 pages.
- The audio probe runs at most once per heartbeat, only while idle.
- Bench: measures the installed daemon in place or a sandboxed second daemon (`-Own`, `-Profile`, `STC_NS`), reports working set, private working set, private bytes, exact CPU from the cycle counter, disk writes per span change, and per-minute event counts. `make profile` builds the counting daemon.

## 0.3.0 (2026-11-09)
- New readable terminal output: bold title with a rule, dim section rules, 16-color stacked bars (solid = active, shaded = passive), trend arrows, sparklines and a weekday x hour heatmap. Unicode and color when the output is a console; plain ASCII and no color when piped or when `NO_COLOR` is set (`FORCE_COLOR` forces Unicode when piping).
- New commands: `report` (with `--days N`, `--app NAME`, `--by category`, `--top N`, `--json`), `heatmap`, `trends`, `focus`, `switches`, `topics`. `today`, `week`, `month` are shortcuts for `report`. Every command supports `--json` with a `"schema": 1` field.
- Categories and topic rules come from `config.ini` `[categories]` and `[topics]` (built-in defaults when absent).
- Uninstall now has two modes and asks y/N first: `uninstall` removes the application and keeps your data; `uninstall --clean` also deletes the database, backups and `config.ini` (only files this program created; `--yes` skips the prompt; `--purge` is an alias of `--clean`).
- `status`, `help`, `doctor` restyled.
- Under the hood: single-pass `stc_scan` feeds every report; each insight is one file plus one line in `insights/registry.c`.

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
