# Changelog

All notable changes to ztodo. The version is the single
`project(ztodo VERSION x.y.z)` in `CMakeLists.txt`; every change bumps it
([semantic versioning](https://semver.org/)) and adds an entry here. Releases
are tagged `vX.Y.Z`, built and published by `.github/workflows/release.yml`,
and signed with minisign (see [docs/RELEASING.md](docs/RELEASING.md)).

## 1.0.2 — 2026-11-25

### Fixed
- **Dialogs were hard to see over the main window.** Every dialog (Help → Check for Updates, Preferences, Edit Task, confirmations) had exactly the window's background colour, so on desktops that draw no strong frame or shadow it blended into the window behind it. Dialogs now have a surface of their own: a lighter grey in dark mode, a slightly darker grey in light mode, with buttons kept distinct from it. It follows theme changes, including while a dialog is open. The native macOS style is left as it is.

### Added
- A UI test that a dialog's background differs clearly from the main window's in both themes and after a switch.

## 1.0.1 — 2026-11-25

### Fixed
- **Monthly tasks drifted to the 28th.** A task repeating `every month` from the 31st moved to Feb 28 and then stayed on the 28th for good (Mar 28, Apr 28, …); the 29th and 30th drifted the same way, as did a yearly task on Feb 29. Each task now remembers the day it repeats on (a new `anchor` field in the log, written with every date change), so it goes Jan 31 → Feb 28 → Mar 31 → Apr 30 → May 31, and Feb 29 returns in leap years. Setting a date by hand makes that day the new one. Tasks already on the 28th after drifting stay there until you set their date once. Devices still on 1.0.0 ignore the new field and keep the old behaviour; a date they change is never pulled to another day.

### Added
- Recurrence and Store tests for month-end and leap-day dates, and fuzz checks with an anchor day.

## 1.0.0 — 2026-11-25

The first stable release. The data folder format is unchanged (`ztodo.json` version 1), so 0.x devices on the same folder keep working alongside it.

### Fixed
- **A task could disappear from every list after a sync.** If one device deleted a project while another, offline, added or moved a task into it, the merged task pointed at a project that no longer existed: it showed in no project and not in the Inbox (only in Today, Upcoming or search, if at all), with a blank project name. Such a task now reads as an Inbox task on every device. Nothing is rewritten, so undoing the project's deletion puts the task back in it.
- Reopening a completed task that has a repeat rule was offered as "Undo Complete" instead of "Undo Reopen". The window now uses the same test as completing does: only an open task with a repeat rule ztodo understands moves to its next date.

### Added
- A Store test for the deleted-project case above.

## 0.5.0 — 2026-11-19

### Added
- **Productivity stats** (View → Productivity, Ctrl+Shift+P): completed today, this week and in the last 30 days, the current and longest day streak, a 14-day chart, completions by project, the best day, and open/overdue counts. The window updates live, including changes synced from other devices. It is all worked out locally from the data folder.
- Every completion is now recorded as a `completion` entry in the device log (docs/STORAGE.md), so each completion of a recurring task counts, not just the latest. Reopening or undoing a completion removes its entry. Devices still on 0.4.x skip these lines and are otherwise unaffected. History from before this version comes from each task's completion time, so a recurring task contributes only its most recent earlier completion.
- `tests/tst_stats`, plus Store and UI tests for completion entries.

## 0.4.2 — 2026-11-19

### Fixed
- **A single far-future timestamp in a synced log could stop all later edits from saving, on every device.** Every device moved its clock up to the highest timestamp it read. A line with the largest possible timestamp (from corruption, or anyone with write access to the Box folder) made the next write's timestamp overflow, and readers then rejected every write from that point on. Timestamps above 2^53 are now skipped, and a device's clock only follows timestamps up to a year ahead of its wall clock. Merging is unchanged. Found by the new log fuzzer.
- Two projects with the same name and position (for example `#Work` quick-added on two devices while offline), or two labels whose names differ only in case, could be listed in a different order on each device. Quick add then filed `#Work` tasks under a different project on each device. Ties now break on the id, so every device picks the same one. Found by the new merge test.

### Added
- `tests/tst_merge`: randomized checks that merging gives the same result whatever order, duplication or split of the logs, and that compaction never changes it, against a plain last-writer-wins model.
- `fuzz/`: libFuzzer harnesses for the device-log reader (including a compaction check), the quick-add, date and recurrence parsers, and the updater's release JSON, `SHA256SUMS`, minisign signature and version parsing. Their seed corpus runs as tests in every build; a new CI job fuzzes each for 2 minutes.

## 0.4.1 — 2026-11-19

### Fixed
- README: the macOS first-launch steps. Right-click → Open no longer gets past Gatekeeper on macOS 15 (Sequoia) and later; the README now gives the System Settings → Privacy & Security → **Open Anyway** steps, the macOS 14 steps, and an `xattr` one-liner.
- SPEC.md said version 0.3.0. `scripts/check-version.sh` now also checks the SPEC.md version line, so it can't fall behind again.

### Added
- `-DZTODO_SANITIZE=ON` builds with AddressSanitizer and UndefinedBehaviorSanitizer. A new CI job runs the whole test suite that way on Linux, with leak checking.

## 0.4.0 — 2026-11-19

### Added
- **Font setting** (Preferences → Font and Size): choose a typeface, or keep the system default, and pick Small, Medium or Large. Medium is the platform default size (the look before this setting existed); Small is 2 points smaller (not below 8 pt) and Large is 2 points larger. It applies immediately, no restart.

## 0.3.1 — 2026-11-18

### Fixed
- The Edit Task dialog showed a literal '&' in the Due and Repeat labels. Those labels now have buddy widgets, so the mnemonic is underlined instead of drawn as text.

## 0.3.0 — 2026-11-18

### Added
- **Appearance setting** (Preferences → Appearance): follow the system, or force Light or Dark. It applies immediately, no restart. Useful on Linux desktops that don't report a dark theme to Qt.

### Fixed
- The sidebar and task list now re-colour when the theme changes while ztodo is running (including when the desktop itself switches), instead of keeping the colours they were created with.
- Due-date colours (overdue, today, tomorrow, this week) are lighter on a dark background, so the date under a selected task stays readable.

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
