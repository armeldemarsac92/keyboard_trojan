# SQLite OOM During INSERT (8192 bytes) Notes

Branch: `fix/top5-hardening`

## Symptom

On-device logs show repeated SQLite `SQLITE_NOMEM (7)` during inserts:

- `(7) failed to allocate 8192 bytes of memory`
- `statement aborts ... out of memory; [INSERT INTO Inputs ...]`
- followed by `[DB] Transaction failed, re-queueing rows.`

## Root Cause

The ArduinoSQLite VFS (`ArduinoSQLite_vfs.cpp`) allocates a rollback-journal write buffer when opening a main journal file:

- It calls `sqlite3_malloc(SQLITE_VFS_JOURNAL_BUFFERSZ)` when `SQLITE_OPEN_MAIN_JOURNAL` is set.
- Default `SQLITE_VFS_JOURNAL_BUFFERSZ` is `8192`.

On a fragmented / constrained Teensy heap, a single contiguous 8KB allocation can fail, causing the transaction to abort.

## Fix Implemented

Reduce the VFS journal buffer size at compile time:

- Added `-D SQLITE_VFS_JOURNAL_BUFFERSZ=2048` to `Keyboard/platformio.ini`.

Also reduced log spam on failure:

- `executeInsertTransaction(...)` now checks `sqlite3_get_autocommit(db)` before issuing `ROLLBACK;` to avoid
  `cannot rollback - no transaction is active` errors when the transaction never fully started.

## Notes / Follow-ups

If you still hit `SQLITE_NOMEM` with `2048`, lower further (e.g. `1024` or `512`) at the cost of more journal writes
and potentially slower commits.

