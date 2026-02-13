# DB Query Jobs (Avoid Blocking The RAK Listener Thread)

## Symptom
- During `[QUERY]` sessions, selecting a table (or running `RANDOM/COUNT/ROW/...`) could stall the app flow.
- Logs typically stop at:
  - `[DB][Q] RANDOM Inputs`
  - and then no further `[RAK][TX] ...` even though `[RAK][TXQ] ...` messages were enqueued.

## Root Cause
- `RakManager::listenerThread()` owns `mt_loop()` + `RakTransport::tick()` (Meshtastic RX/TX).
- `RakManager::processCommands()` was doing synchronous SQLite reads (e.g. `randomRow()`, `countRows()`, `rowByRowid()`).
- Any slow/hanging SQLite call blocks the listener thread, which prevents `transport_.tick()` from running, so outbound DM replies never get transmitted.

Additional risk:
- The ArduinoSQLite build might be configured with `SQLITE_THREADSAFE=0` (single-thread assumptions). Even if guarded with an external mutex, using one `sqlite3*` from multiple threads is risky/undefined. We now log `sqlite3_threadsafe()` once at DB init:
  - `[DB] sqlite3_threadsafe=<0|1|2>`

## Fix Implemented
- All interactive DB reads for the RAK command protocol are now executed on the existing DB writer thread (the thread that calls `DatabaseManager::processQueue()`).
- A small async "DB job" queue was added in `DatabaseManager`.
  - `RakManager` enqueues DB jobs (non-blocking).
  - The DB thread processes at most 1 job per `processQueue()` call.
  - Results are sent back via a callback that enqueues radio text messages (thread-safe) without calling `transport_.tick()` from the DB thread.

## Code Touch Points
- `Keyboard/include/DatabaseManager.h`
  - Added job queue + enqueue APIs (`enqueueRandomRow`, `enqueueCountRows`, etc.)
  - Added `setReplyCallback()` used to hand results back to the radio layer.
- `Keyboard/src/DatabaseManager.cpp`
  - Implements job queue and processes jobs inside `processQueue()`.
- `Keyboard/include/RakManager.h`, `Keyboard/src/RakManager.cpp`
  - Sets the DB reply callback to `RakManager::dbReplyCallback()`.
  - Replaced synchronous DB reads in the listener thread with job enqueues.

## Expected Behavior After Fix
- `[QUERY]` table selection replies should be sent reliably (no more "stuck after RANDOM").
- Typing latency should improve because Meshtastic TX/RX is no longer blocked by DB reads.

## Follow-Ups (Optional)
- If interactive commands are spammed, the DB job queue can fill (currently bounded). We reply with a "queue full" message in that case.
- If some DB jobs are still slow (e.g. `COUNT(*)` on large tables), consider:
  - making `COUNT` approximate by default (rowid max), and adding `COUNT!` for exact.
  - adding a time budget / watchdog for very slow SQLite calls.
