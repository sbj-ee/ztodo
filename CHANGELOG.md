# Changelog

All notable changes to ztodo. The version is the single
`project(ztodo VERSION x.y.z)` in `CMakeLists.txt`; every change bumps it
([semantic versioning](https://semver.org/)) and adds an entry here. Releases
are tagged `vX.Y.Z`, built and published by `.github/workflows/release.yml`,
and signed with minisign (see [docs/RELEASING.md](docs/RELEASING.md)).

## 0.1.0 — unreleased

First version.

### Added
- Todoist-style views: Inbox, Today (overdue first), Upcoming (by day), Completed, projects, labels and search.
- Quick add in natural language: dates, times, repeat rules, `p1`–`p4`, `#project`, `@label`, with a live preview.
- Tasks with notes, due date and time, repeat rules (`every!` repeats from completion), priority, project, labels and subtasks; manual ordering in the Inbox and projects.
- Undo for every change.
- Storage in a Box-synced folder: one append-only log per device, merged last-writer-wins per field, so syncing never produces conflicts. Optional sync command (e.g. an rclone bisync unit) after changes.
- Signed auto-update from GitHub releases: `pkexec apt install` on Linux, in-place `ztodo.app` replacement on macOS.
- Green Zorro "Z" app icon matching zterminal.
