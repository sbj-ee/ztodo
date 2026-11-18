# Changelog

All notable changes to ztodo. The version is the single
`project(ztodo VERSION x.y.z)` in `CMakeLists.txt`; every change bumps it
([semantic versioning](https://semver.org/)) and adds an entry here. Releases
are tagged `vX.Y.Z`, built and published by `.github/workflows/release.yml`,
and signed with minisign (see [docs/RELEASING.md](docs/RELEASING.md)).

## 0.2.0 — 2026-11-18

### Added
- **Reminders:** a desktop notification when a task with a due time is due, optionally 5–60 minutes early (Preferences → Remind me). Fires once per due moment, skips reminders more than 10 minutes late (no burst at startup), and fires again when a task is rescheduled or a recurring task moves on.

### Changed
- Much faster with large lists: the task, project and label lists and the subtask index are cached until the data changes, instead of being rebuilt on every call (the main window called them in loops).
- Deleting a task with subtasks, a project, and Undo now each refresh the window once instead of once per item.
- Log files are read incrementally: only lines appended since the last scan are parsed. A rewritten (compacted) log is detected and read in full, so nothing is missed.
- Internal: the undo history is now `core/UndoStack` (no widgets, unit-tested), and the sidebar/project/label code moved from `MainWindow.cpp` to `MainWindowNav.cpp`.

## 0.1.3 — 2026-11-12

### Fixed
- The sidebar (Inbox, Today, Upcoming, Completed, projects, labels) was empty when ztodo opened and only appeared after the first change. It is now filled when the window opens.

### Changed
- Higher-contrast sidebar: a background distinct from the task pane with a divider line, darker (light theme) or brighter (dark theme) section headers, counts and "Add project" (7:1 or better against the sidebar, up from about 3.4:1), a stronger selection highlight and a bold name for the selected view.

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
