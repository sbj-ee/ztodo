# ztodo specification

Version 0.3.0. This document describes what ztodo does and the rules it keeps.
It is the reference for behaviour; [README.md](README.md) is the user guide,
[docs/STORAGE.md](docs/STORAGE.md) the on-disk format in detail, and
[docs/RELEASING.md](docs/RELEASING.md) the release process.

## 1. Purpose and scope

ztodo is a Todoist-style to-do app for **Linux (amd64)** and **macOS (Apple
Silicon, 14+)**, written in C++20 with Qt 6 Widgets. Tasks are kept in a folder
that a third-party sync client (Box Drive on macOS, rclone on Linux) copies
between a person's own machines.

Goals:

- One shared task list across several of the user's devices, with no account
  and no server besides Box.
- Fast capture: typing a sentence creates a fully formed task.
- Edits made offline on two devices never produce a sync conflict, and never
  lose an edit to a different field.
- Updates install only when authentic (signed).

Non-goals:

- Multi-user collaboration or sharing tasks with other people.
- Talking to Box (or any cloud API) directly. ztodo only reads and writes a
  normal folder.
- Mobile or web clients. No Windows build.
- Notifications while ztodo is not running.

## 2. Architecture

Two static libraries, one executable, and tests:

| Target | Directory | Depends on | Contents |
|---|---|---|---|
| `ztodo_core` | `core/` | Qt Core, Qt Network, libsodium | Everything that is not a widget: `Store`, `UndoStack`, `Views`, `QuickAdd`, `DateText`, `Recurrence`, `Reminders`, `Settings`, `Update`, `UpdateChecker`, `Minisign` |
| `ztodo_ui` | `app/` | `ztodo_core`, Qt Widgets | `MainWindow` (+ `MainWindowNav.cpp` for the sidebar and project/label management), `TaskDialog`, `SettingsDialog`, `UpdateDialog`, `UpdateManager`, `Delegates`, `Theme` |
| `ztodo` | `app/main.cpp` | `ztodo_ui` | Entry point |
| `tst_*` | `tests/` | Qt Test | `tst_store`, `tst_parsing`, `tst_views`, `tst_reminders`, `tst_update`, `tst_ui` (offscreen) |

Rule: logic goes in `core/` and is unit-tested without a GUI; `app/` only
presents it and routes user actions to it.

### Process model

- Single process, single thread (Qt event loop). Network I/O is asynchronous.
- **One instance per device.** `main()` takes a `QLockFile`
  (`<config>/ztodo.lock`); a second instance shows "ztodo is already running"
  and exits. Two instances would append to the same log.
- Command line: `--version`/`-v`, `--help`/`-h`. Nothing else.

### Startup sequence

1. Take the lock; load settings.
2. If no data folder is set, show the welcome/Preferences dialog (Quit exits).
   Open the store; on failure show the error and ask again.
3. If this device's log is over 256 KB, compact it.
4. Show the main window; 500 ms later run the sync command (if any); schedule
   the once-a-day update check.

## 3. Data model

Three entity kinds, each identified by a random id (`newId()`: 20 hex
characters of a UUID). Ids and device ids are restricted to ASCII letters,
digits, `-`, `_`, at most 64 characters (they appear in file names).

**Task**

| Field | Type | Meaning |
|---|---|---|
| `title` | string | Required for the task to exist |
| `notes` | string | Free text |
| `project` | project id | Empty = Inbox |
| `parent` | task id | Empty = top level; otherwise a subtask |
| `labels` | array of label ids | |
| `priority` | 1..4 | 1 = highest (red), 4 = none. Out-of-range values are clamped |
| `due` | string | `YYYY-MM-DD` (all day) or `YYYY-MM-DDTHH:mm` (local time); empty = no date |
| `recur` | string | Recurrence rule text (§6); empty = once |
| `done`, `doneAt` | bool, ISO 8601 UTC | |
| `created` | ISO 8601 UTC | |
| `order` | number | Manual order; sorts ascending, ties broken by id |
| `deleted` | bool | Tombstone |

**Project:** `name`, `color` (`#rrggbb`, default `#4fc33a`), `order`, `deleted`.
**Label:** `name`, `color` (default `#8a8f98`), `deleted`. Labels sort by name.

A task that has no `title` field is treated as not existing. A project or label
with an empty name is ignored.

Name matching for `#project` and `@label` ignores case, spaces and underscores
(`Home Improvement`, `home_improvement` and `homeimprovement` are the same).

## 4. Storage and sync

The full format is in [docs/STORAGE.md](docs/STORAGE.md). The rules that matter:

1. **One writer per file.** Each device appends only to its own
   `devices/<device-id>.jsonl`. The marker `ztodo.json`
   (`{"format":"ztodo","version":1}`) is written once.
2. **Op log.** A line is one change: `{"ts","dev","k","id","f"}`, setting the
   fields in `f` on entity `id` of kind `k`. A line that fails validation
   (bad JSON, negative `ts`, invalid id/kind/device) is skipped.
3. **Hybrid clock.** `ts` = `max(wall clock ms, highest ts seen + 1)`, so a
   device with a slow clock still overrides what it has already read.
4. **Merge: last writer wins per field.** The value from the change with the
   highest `(ts, dev)` wins; the device id breaks exact ties. The result does
   not depend on the order files or lines are read, and reading a change twice
   has no effect (idempotent).
5. **Every `*.jsonl*` file in `devices/` is read**, including sync conflict
   copies (`x.jsonl.conflict1`). Duplicates are harmless because of rule 4.
6. **Torn lines.** A last line without its newline (still being written or
   synced) is not applied until it is complete. Before appending, a device ends
   a torn last line of its own log with a newline so the new op is not lost.
7. **Deletes are tombstones** (`deleted: true`). Deleting a task also deletes
   all of its subtasks (at any depth). A tombstone is an ordinary change, so it
   syncs and can be undone.
8. **Format version.** A data folder whose marker says version greater than 1
   is refused with "written by a newer ztodo".
9. **Compaction.** At startup, when this device's log exceeds 256 KB, it is
   rewritten without the fields that a newer change (from any device) has
   replaced. A change is dropped only after its replacement has been read, so
   the merged state never changes. The rewrite is an atomic rename.
10. **Reading changes.** The folder is watched, and also re-scanned every 15
    seconds (watchers miss changes on FUSE and File Provider mounts). Reloads
    from the watcher are debounced by 400 ms. A file is re-read only if its
    size or modification time changed, and then **only from the last complete
    line already applied**; if the 32 bytes before that offset have changed
    (the log was compacted or replaced) the whole file is read again.
11. **Device id.** `<hostname>-<8 random chars>`, created on first run and stored
    in `<config>/device-id`. It must never be copied to another machine.

### Sync command (optional)

A shell command (typically starting rclone `bisync`) run at startup, on
**File → Sync Now** (F5), and 5 seconds after a change (restarted by further
changes). After it runs, the store is re-read at once and again after 4, 15 and
45 seconds. On macOS it is left empty because Box Drive syncs by itself.

## 5. Views

| View | Shows | Order |
|---|---|---|
| Inbox | Open tasks with no project | Manual; subtasks nested under parents |
| Project | Open tasks in that project | Manual; subtasks nested |
| Today | Open tasks due today or earlier | "Overdue" section (by date, then priority), then "Today" section (by priority, then timed before all-day, then time, then manual order) |
| Upcoming | Open tasks due after today | Grouped by day, sorted by date, then priority |
| Label | Open tasks with the label | Dated first, then by due |
| Completed | Done tasks | By completion time, newest first, grouped by day; at most 500 |
| Search | Title or notes contain the text, case-insensitive | Open before completed |

- Sidebar badges show the number of open tasks per view (none for Completed and
  Search).
- A subtask whose parent is not in the view is shown at the top level; parent
  cycles in bad data cannot loop. Nesting displays at most 4 levels deep.
- Subtasks follow their parent when its project changes. A task moved to a
  project other than its parent's project is detached from the parent.
- Manual reorder (Alt+↑/↓) works only in Inbox and project views.
- The last-used view and window geometry are remembered (per device).
- The main window recomputes at midnight (checked each minute).

## 6. Input language

### Quick add

Typing in the quick-add box shows a live preview of what was recognised;
Enter creates the task. Recognised parts are removed from the title (if
nothing is left, the typed text is the title). New tasks go to the current
view's project, or to the project named with `#`, creating it if needed.

| Token | Meaning |
|---|---|
| `p1` … `p4` | Priority |
| `#Project` | Project; `_` stands for a space |
| `@label` | Label (repeatable); `_` stands for a space |
| A date | `today`/`tod`, `tomorrow`/`tmr`, `yesterday`, `mon`…`sun` (on or after today), `next mon`, `next week` (Monday), `next month` (1st), `in 3 days/weeks/months`, `eom`, `nov 20`, `20 nov`, `nov 20 2027`, `11/20`, `11/20/2027`, `2026-11-20`. A month-day already past means next year |
| A time | `5pm`, `5:30pm`, `at 17:00`, `at 9`, `noon`, `midnight` |
| A recurrence | See below |

Rules: matching is case-insensitive and on whole words. A time with no date
means today. A recurrence with no date takes its first occurrence on or after
today. A bare `daily`/`weekly`/`monthly` counts as a recurrence only at the end
of the text (before any `#`, `@` or `p1` tokens), so "Review monthly budget" is
a title.

### Recurrence

`every day | daily | every 3 days | every other day`,
`every week | weekly | every 2 weeks | every other week`,
`every month | monthly | every 3 months | every 15th`,
`every year | yearly | annually`,
`every weekday | every workday | every weekend`,
`every mon | every mon, wed and fri | every tuesday`.

`every!` counts from the completion date instead of the due date
(`every! 3 days`). Completing a recurring task does not mark it done: it moves
to its next date (always after today) and records `doneAt`.

### Task editor

The due field accepts the same date and time words; the whole text must be a
date (and optional time), otherwise it is rejected. Empty clears the date.

## 7. Operations

- **Complete / reopen** (Space). Recurring tasks move to their next date.
- **Add subtask**, **edit** (Enter / double-click), **delete** (Del; subtasks too),
  **move to project**, **set priority**, **reorder**.
- **Projects and labels:** add, rename, recolor, delete. Deleting a project
  deletes its tasks after confirmation; deleting a label only removes the label
  (tasks keep their other labels).
- **Undo** (Ctrl+Z, or the Undo button for 8 seconds after an action). Every
  action above is undoable. Undo is a list of "put these old field values
  back" changes (`UndoStack`), applied last-first as one batch, and is itself
  written to the log like any other change. The history keeps 100 entries and is
  per session (cleared when the data folder changes).
- **Batching.** Multi-entity actions (delete with subtasks, delete project,
  undo) emit one `changed()` signal so the window refreshes once.
- **Keyboard:** Q or Ctrl+N quick add · Space complete · Enter edit · Del delete ·
  Alt+↑/↓ reorder · Ctrl+Z undo · Ctrl+F search · Ctrl+1…4 views · F5 sync.

## 8. Reminders

- Only tasks with a due **time** are reminded; all-day tasks are not.
- Checked every 20 seconds while ztodo runs, if enabled (default on).
- A task fires at `due − lead`, where lead is 0, 5, 10, 15, 30 or 60 minutes
  (Preferences, default 0).
- Fires **once per (task, due date and time)**. Rescheduling a task, or a
  recurring task moving to its next date, is a new due moment and fires again.
- A reminder more than **10 minutes late** is skipped, so reopening the app
  does not produce a burst of old notifications. Completed tasks never fire.
- Several reminders in one check are shown as one notification, ordered by time.
- Delivery: a system tray notification; if no system tray is available, a message
  in the status bar. The window is also flagged for attention.
- Each device reminds for itself; reminder state is not synced and not stored
  (a restart within the 10-minute window can repeat a reminder).

## 9. Settings and files

Per device, not synced.

**Appearance.** Preferences → Appearance is *Follow the system* (default),
*Light* or *Dark*. A forced theme uses the Fusion style with a built-in light or
dark palette and applies immediately. The sidebar and task list re-colour on any
palette change, including a desktop theme switch. Choosing *Follow the system*
again restores the platform's style and palette; live following of the desktop
theme resumes after a restart. Due-date colours are lighter on a dark background.

| What | Where |
|---|---|
| Settings | `<config>/ztodo.ini` |
| Device id | `<config>/device-id` |
| Update state (last check, skipped version) | `<config>/update-state.ini` |
| Instance lock | `<config>/ztodo.lock` |
| Tasks (shared) | `<data folder>/devices/*.jsonl`, `<data folder>/ztodo.json` |

`<config>` is `$XDG_CONFIG_HOME/ztodo` if set, else `~/.config/ztodo` (Linux)
or `~/Library/Preferences/ztodo` (macOS).

Settings keys: `data/dir`, `data/syncCommand`, `updates/checkOnStartup`
(default true), `ui/appearance` (`system`, `light` or `dark`; default `system`), `reminders/enabled` (default true), `reminders/leadMinutes`
(default 0, 0–1440).

The data folder is suggested as `<Box folder>/ztodo`, where the Box folder is
`~/Library/CloudStorage/Box-Box`, `~/Box` or `~/Box Sync` (macOS) or `~/Box` or
`~/box` (Linux). On Linux, if `~/.config/systemd/user/boxsync.service` exists,
`systemctl --user start --no-block boxsync.service` is suggested as the sync
command.

## 10. Signed updates

Nothing is installed unless it is proven to come from the release key.

1. **Check.** Once a day at startup (if enabled; a clock set back also allows a
   check) and on **Help → Check for Updates**, ztodo asks the GitHub API for
   `sbj-ee/ztodo` latest release. A release is offered if it is newer by
   semantic version (prereleases rank below their release; `~` is accepted as
   the Debian prerelease separator) and is not a draft. A skipped version stays
   quiet for automatic checks, and anything newer is offered again. A manual
   check always offers.
2. **Trusted URLs only.** The release page opens only if it is
   `https://github.com/sbj-ee/ztodo/releases/…`. An asset is downloaded only from
   exactly `https://github.com/sbj-ee/ztodo/releases/download/<tag>/<name>`
   (no query, fragment or user info; plain path segments). Redirects are allowed
   only to GitHub's release-asset hosts over https.
3. **Verify.** `SHA256SUMS` must carry a valid minisign signature
   (`SHA256SUMS.minisig`) by the Ed25519 key compiled into the app
   (`core/UpdateSigningKey.cpp`; key ID `32960BBA77B36F1F`), and the signed trusted
   comment must be exactly `ztodo <tag> SHA256SUMS` (binding the signature to the
   tag and the app). The package's SHA-256 must then match its line in
   `SHA256SUMS`. Any failure stops the install. A build with no key compiled in
   refuses to install.
4. **Install.**
   - Linux: `/usr/bin/pkexec /usr/bin/apt install <deb>` (absolute paths). If
     pkexec or apt is missing, the verified package path and the manual command
     are shown instead.
   - macOS: mount the `.dmg` with `hdiutil`, copy `ztodo.app` with `ditto` to a
     staging path next to the installed bundle, swap it in with renames (the old
     bundle is restored if a step fails), and detach.
5. Asset names: `ztodo_<version>_amd64.deb`, `ztodo-<version>-Darwin.dmg`,
   `SHA256SUMS`, `SHA256SUMS.minisig`.

## 11. Build, test, release

- CMake ≥ 3.21, Qt ≥ 6.4 (Core, Gui, Widgets, Network; Test for tests),
  libsodium ≥ 1.0.18. C++20, no extensions. `-DZTODO_WERROR=ON` in CI.
- `ZTODO_DEV_OVERRIDES` (debug builds only) allows `$ZTODO_UPDATE_URL`; it is
  never in a release.
- Tests run offscreen with `ctest`. Asset downloads and the update flow are tested
  against a local mock server.
- CI (`.github/workflows/ci.yml`): version-drift check, then build, test and
  package on Ubuntu 24.04 and macOS 15 (arm64). Actions are pinned by commit SHA.
- **Versioning.** The single source of truth is `project(ztodo VERSION x.y.z)`.
  Every change bumps it (semantic versioning) and adds a `CHANGELOG.md` entry;
  `scripts/check-version.sh` enforces that CMake, the top changelog heading and
  the README agree.
- **Release.** Tag `vX.Y.Z`. `.github/workflows/release.yml` builds and tests on
  both platforms, writes and signs `SHA256SUMS`, checks the signature against
  the key in the tagged source, and publishes after a reviewer approves the
  protected `release` environment. Without the signing secrets nothing is
  published.

## 12. Known limitations and open questions

- **Tombstones and old logs are never purged.** A deleted task's identity stays
  in the logs forever, and a retired device's log is read by every device.
  Purging safely needs a per-device "seen" watermark.
- **Reminders** only fire while ztodo runs; they are not synced between devices
  and their fired-state is in memory.
- **Quick-add collisions.** `#Work Stuff`, `#work_stuff` and `#workstuff` name
  the same project, so two such projects cannot be told apart in quick add.
- **An invalid `due` string** reads as "no due date" without any message.
- **`order` ties.** `nextOrder` tracks the highest `order` ever applied, so it
  never reuses a number even if the highest task later moves down.
- **No attachments, comments, sections, filters or boards.**
