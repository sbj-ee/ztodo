# Storage format

ztodo's data is a folder that a sync client (Box Drive on macOS, rclone on
Linux) copies between devices. The format is designed so that **no file ever
has two writers**: sync clients make conflict copies when two machines change
the same file, and ztodo never gives them the chance.

```
<data folder>/
  ztodo.json                 {"format":"ztodo","version":1}
  devices/
    laptop-3f9a1c2e.jsonl    one append-only log per device
    macbook-77b0d1aa.jsonl
```

## Device logs

Each device has a random id, `<hostname>-<8 chars>`, created on first run and
stored in `~/.config/ztodo/device-id`. It appends one JSON object per line to
`devices/<id>.jsonl`:

```json
{"ts":1794511200123,"dev":"laptop-3f9a1c2e","k":"task","id":"9f2c…","f":{"title":"Buy milk","priority":1}}
```

| Key | Meaning |
|---|---|
| `ts` | Hybrid clock in ms: `max(wall clock, highest ts seen + 1)`, so a device with a slow clock still overrides what it has already seen |
| `dev` | Writer's device id |
| `k` | `task`, `project` or `label` |
| `id` | Entity id |
| `f` | Fields set by this change |

### Fields

- **task:** `title`, `notes`, `project` (id, empty = Inbox), `parent` (task id,
  for subtasks), `labels` (array of label ids), `priority` (1 highest … 4 none),
  `due` (`YYYY-MM-DD` or `YYYY-MM-DDTHH:mm`, local time), `recur` (rule text,
  e.g. `every 2 weeks`), `done`, `doneAt`, `created` (ISO 8601 UTC), `order`,
  `deleted`
- **project:** `name`, `color`, `order`, `deleted`
- **label:** `name`, `color`, `deleted`

## Merging

Every device reads every file in `devices/` whose name contains `.jsonl`,
including conflict copies such as `x.jsonl.conflict1`. For each field of each
entity, the value from the change with the highest `(ts, dev)` wins. This merge
gives the same result whatever order the files arrive in, and reading a
change twice changes nothing. So:

- devices converge as soon as they have the same files;
- two offline edits to different fields of one task are both kept;
- a duplicate or conflict copy of a log is harmless.

A line without its final newline (a file still being written or synced) is
skipped until it is complete. Deletes are tombstones (`deleted: true`), so a
delete syncs like any other change and can be undone.

## Compaction

At startup, if this device's log is over 256 KB, ztodo rewrites it without the
changes that a newer change (from any device) has replaced. A change is only
dropped once its replacement has been read, so the merged state never changes.
The log is written with an atomic rename.

## Reading changes

ztodo watches the folder and also re-scans it every 15 seconds, because file
watchers miss changes on FUSE mounts and Box Drive's File Provider. Only
files whose size or modification time changed are re-read, and then only from the
last complete line already applied. If the bytes just before that point have
changed (a log that was compacted), the whole file is read again; this is safe
because applying a change twice does nothing.
