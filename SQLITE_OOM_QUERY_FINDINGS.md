# SQLite OOM During SELECT (COUNT / ORDER BY RANDOM) Notes

Branch: `fix/top5-hardening`

## Symptom

During the interactive `[QUERY]` session, the node could hit `SQLITE_NOMEM (7)` on read-only queries, e.g.:

- `SELECT COUNT(*) FROM Inputs;`
- `SELECT rowid, * FROM Inputs ORDER BY RANDOM() LIMIT 1;`

These show up as:

- `(7) failed to allocate XXXX bytes of memory`
- `statement aborts ... out of memory; [...]`

## Likely Root Causes

- **Default SQLite page cache grows too much** on embedded targets during large table scans (e.g. `COUNT(*)` on a big `Inputs` table).
- `ORDER BY RANDOM()` can trigger a **temp sort / temp b-tree**, which is expensive in both CPU and memory.

## Fixes Implemented

### 1) Avoid `ORDER BY RANDOM()` for random-row selection

`DatabaseManager::randomRow(...)` no longer uses `ORDER BY RANDOM()`.

Instead it:

- reads `MIN(rowid)` / `MAX(rowid)` for the table
- samples a few random `rowid` values and tries `WHERE rowid = ?`
- falls back to `ORDER BY rowid DESC LIMIT 1` if rowids are sparse

This keeps the query cheap and avoids large temp allocations.

### 2) Don’t auto-run expensive COUNT on table selection

The QUERY session now reports a **rowid range** on selection (approximate), and keeps exact `COUNT` as an explicit command.

If `COUNT` fails, the firmware falls back to reporting `rowid max` as an approximation.

### 3) Bound the “SECRETS” query

`topSecrets()` now only considers a bounded window (currently last ~2000 rowids) to avoid scanning/grouping the entire `Inputs` table.

### 4) Cap SQLite memory use and release caches

On DB open, the firmware runs:

- `PRAGMA cache_size=-128;` (128 KiB page cache)
- `PRAGMA temp_store=FILE;` (avoid in-memory temp tables when possible)

After DB writes and most query helpers, it calls:

- `sqlite3_db_release_memory(db);`

## If OOM Still Happens

Options to try next:

- Lower `PRAGMA cache_size` further (e.g. `-64`).
- If your Teensy 4.1 has PSRAM, enable ArduinoSQLite EXTMEM allocator (`T41SQLite::begin(..., true)`).
- Provide a fixed page-cache buffer via `sqlite3_config(SQLITE_CONFIG_PAGECACHE, ...)` before `sqlite3_initialize()`.

