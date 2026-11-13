# Changelog

All notable changes to ztodo. The version is the single
`project(ztodo VERSION x.y.z)` in `CMakeLists.txt`; every change bumps it
([semantic versioning](https://semver.org/)) and adds an entry here. Releases
are tagged `vX.Y.Z`, built and published by `.github/workflows/release.yml`,
and signed with minisign (see [docs/RELEASING.md](docs/RELEASING.md)).

## 0.1.2 — 2026-11-12

### Added
- The window title shows the running version and the current view: `ztodo 0.1.2 — Today`.

## 0.1.1 — 2026-11-12

### Fixed
- The menu bar was missing on Linux desktops running a global-menu service (for example a GNOME app-menu extension): Qt handed the menus to that service and hid the in-window bar, so Preferences couldn't be reached. The menu bar now always stays in the window on Linux (macOS keeps its native menu bar).

### Added
- A **Settings** menu: Preferences… (Ctrl+,), Change Data Folder…, Sync Command…, a "Check for Updates at Startup" toggle, Open Data Folder, and Open Settings File Location. Preferences used to be under File.

## 0.1.0 — 2026-11-12

First version.

### Added
- Todoist-style views: Inbox, Today (overdue first), Upcoming (by day), Completed, projects, labels and search.
- Quick add in natural language: dates, times, repeat rules, `p1`–`p4`, `#project`, `@label`, with a live preview.
- Tasks with notes, due date and time, repeat rules (`every!` repeats from completion), priority, project, labels and subtasks; manual ordering in the Inbox and projects.
- Undo for every change.
- Storage in a Box-synced folder: one append-only log per device, merged last-writer-wins per field, so syncing never produces conflicts. Optional sync command (e.g. an rclone bisync unit) after changes.
- Signed auto-update from GitHub releases: `pkexec apt install` on Linux, in-place `ztodo.app` replacement on macOS.
- Green Zorro "Z" app icon matching zterminal.
