# RAK Text Command Protocol (Meshtastic)

Branch: `fix/top5-hardening`

## Goal

Provide a simple, master-only command listener over Meshtastic `TEXT_MESSAGE_APP` to query high-level DB state and schema.

Important constraints:

- No Meshtastic sends from inside receive callbacks (library uses a shared RX/TX protobuf buffer).
- Text messages are limited to ~233 bytes payload; responses are sent as multiple short lines.

## Transport

- Inbound: Meshtastic `TEXT_MESSAGE_APP` callback (`RakManager::onTextMessage`)
- Outbound: queued via `RakTransport::enqueueText(...)` and sent from `RakManager::listenerThread()`

## Security / Access Control

- Commands are only accepted via **direct messages** to `my_node_num`.
- Commands are only executed if the sender is an enrolled **master** (`RadioMasters` / `mastersAddresses`).
- Non-masters receive: `Unauthorized. Pair first (DM): PAIR:<secret>`

## Pairing (Master Enrollment)

Enrollment message format:

- `PAIR:<secret>`

Notes:

- The secret is configured at build time in `Keyboard/config/KeyboardConfig.h` as `KeyboardConfig::Security::MasterEnrollmentSecret`.
- Enrollment is intentionally **disabled** until you replace the placeholder secret and meet the minimum length.
- Send the pairing message as a **direct message** to this node (i.e. `to == my_node_num`).
- Channel: the firmware does not hardcode a channel index, but the DM must be sent on a channel where both nodes can decrypt it (typically channel `0` / your primary shared channel). Replies are sent back on the same channel as the inbound DM.

## Commands

All commands are bracketed and case-insensitive:

- `[HELP]` or `[QUERY]`
  - Shows available tables and example commands.
- `[TABLES]`
  - Lists tables exposed by the firmware.
- `[SCHEMA <table>]`
  - Prints the compile-time schema (from `KeyboardConfig::Tables`).
- `[COUNT <table>]`
  - Executes `SELECT COUNT(*)` and returns the row count.
- `[TAIL Inputs <n>]`
  - Returns up to 10 latest rows from `Inputs` (`InputID`, `Timestamp`, `Input`), with input truncated.
- `[TAIL RadioMasters]`
  - Lists enrolled masters (id/address).
- `[TYPE]` (start typing session)
  - Opens a typing session (master-only). Any subsequent direct messages from that master are injected into the host as USB keyboard input.
  - Only one typing session can be active at a time (owned by the master who started it).
  - End the session with `[/TYPE]` or after 5 minutes of inactivity.
  - While the session is active, `TEXT_MESSAGE_APP` DMs from the session owner are treated as typed text. Only `[TYPE]` and `[/TYPE]` are reserved; other bracketed commands like `[HELP]` will be typed, not executed.
  - Minimal escapes supported in typed text: `\\n` (Enter), `\\t` (Tab), `\\r`, `\\\\`.
  - Layout: injection uses the firmware's **AZERTY** key mapping (`AzertyLayout`), so the host should be configured for AZERTY to get the intended characters.
- `[/TYPE]` (end typing session)
  - Ends the active typing session and clears any queued injected text.

## Files

- `Keyboard/src/RakManager.cpp`
  - parsing + queueing + command execution
- `Keyboard/include/RakManager.h`
  - command queue storage
- `Keyboard/src/DatabaseManager.cpp`
  - `countRows(...)`, `tailInputs(...)`
- `Keyboard/include/DatabaseManager.h`
  - query helper declarations
