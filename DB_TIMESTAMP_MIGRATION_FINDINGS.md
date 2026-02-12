# DB Timestamp Migration (OOM) Notes

Branch: `fix/top5-hardening`

Status: legacy timestamp migration code was removed after completing the one-time migration (commit `3f8a8b3`).
This document remains as historical context in case an older DB reappears.

## Symptom

Device logs show SQLite `SQLITE_NOMEM (7)` during the legacy timestamp migration:

- `(7) failed to allocate 8192 bytes of memory`
- `statement aborts ... out of memory; [UPDATE Inputs SET Timestamp = CAST(Timestamp AS REAL) / 1000000.0 WHERE rowid IN (... LIMIT 200);]`
- `[DB] Timestamp migration failed: out of memory`

Keyboard stays functional, but the migration never completes and the error can reappear periodically.

## Root Cause

The original migration step used a single `sqlite3_exec()` with a complex statement:

- `UPDATE ... WHERE rowid IN (SELECT ... LIMIT N)`
- plus `CAST(...)` in both the `SET` and `WHERE` clauses.

On the Teensy/Arduino heap, this can require extra temporary allocations inside SQLite (query planner + subquery temp results), and it can fail with `SQLITE_NOMEM` even for small batches (e.g. 200 rows).

## Fix Implemented

Replaced the migration step with a lower-memory approach using prepared statements:

1. `SELECT rowid FROM Inputs WHERE Timestamp > 10000 LIMIT ?`
2. `BEGIN TRANSACTION`
3. For each selected `rowid`: `UPDATE Inputs SET Timestamp = Timestamp / 1e6 WHERE rowid = ?`
4. `COMMIT`

Additional safeguards:

- Hard cap per step (`kMaxRowsPerStep = 32`) to bound work and memory.
- If `SQLITE_NOMEM` occurs, the migration returns `-2`.
- `DatabaseManager::processQueue()` disables further migration attempts on `-2` to avoid repeated heap fragmentation and log spam.

Code reference:

- `Keyboard/src/DatabaseManager.cpp` (`migrateInputTimestampsToSecondsStep`)
- `Keyboard/src/DatabaseManager.cpp` (`DatabaseManager::processQueue`)

## Operational Note

If you still see `[DB] Timestamp migration disabled (out of memory).`:

- New rows are still stored in seconds.
- Legacy rows may remain in microseconds.
- If legacy data matters, consider migrating `logger.db` off-device (desktop SQLite) or deleting it so the firmware recreates a fresh DB with seconds-based timestamps.
