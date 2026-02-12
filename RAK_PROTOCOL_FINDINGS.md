# RakManager Reliable Transport Notes

This file captures ongoing findings/design notes while building a more robust `RakManager` transport protocol (fragmentation, retries, ACKs, reassembly) on top of the Meshtastic Arduino/C++ client library.

Branch: `fix/top5-hardening`

## Goal

Implement an application-layer "reliable transport" for payloads larger than a single Meshtastic packet:

- Split a payload into chunks (fragmentation)
- Send chunks over Meshtastic
- Receiver reassembles payload
- Receiver ACKs what it got (and optionally NACKs what it missed)
- Sender retries missing chunks with backoff/timeouts
- Provide a clean wrapper API (e.g. `sendPayload(dest, bytes)` + receive callback)

Important: Meshtastic/LoRa already has its own routing/retry logic for `want_ack`, but we still need app-level ACKs for:

- Multi-chunk payload completion
- Explicit confirmation that the *whole* payload was received and reassembled

## Local Library (what we have in this repo)

PlatformIO dependency: `Keyboard/.pio/libdeps/teensy41/Meshtastic`

- Version: `0.0.7` (see `Keyboard/.pio/libdeps/teensy41/Meshtastic/library.properties`)
- This is the Meshtastic *client* library (serial/wifi) not the radio firmware.

### Public API surface (Meshtastic-arduino)

From `Keyboard/.pio/libdeps/teensy41/Meshtastic/src/Meshtastic.h`:

- `mt_serial_init(rx_pin, tx_pin, baud)`
- `mt_loop(now)`
- `mt_request_node_report(cb)`
- `set_text_message_callback(cb)`
- `set_portnum_callback(cb)` (gives `meshtastic_PortNum` + raw `meshtastic_Data_payload_t*`)
- `set_encrypted_callback(cb)`
- `mt_send_text(text, dest, channel_index)`

Notably: there is no public `mt_send_bytes()` or `mt_send_packet()` helper; only `mt_send_text()`.

### `mt_send_text()` behavior (important)

In `Keyboard/.pio/libdeps/teensy41/Meshtastic/src/mt_protocol.cpp`:

- Builds a `meshtastic_MeshPacket` with:
  - `which_payload_variant = decoded`
  - `decoded.portnum = TEXT_MESSAGE_APP`
  - `to = dest`
  - `channel = channel_index`
  - `want_ack = true`
  - `id = random(0x7FFFFFFF)`
- Copies `strlen(text)` bytes into `decoded.payload.bytes`.

Risk: there is no bounds-check against `meshtastic_Data_payload_t` maximum size, so passing a long string could overflow the nanopb payload buffer.

### Protobuf size/MTU facts (from generated code)

From `Keyboard/.pio/libdeps/teensy41/Meshtastic/src/meshtastic/mesh.pb.h`:

- `meshtastic_Data_payload_t` is `PB_BYTES_ARRAY_T(233)`
  - So max payload per decoded message is ~233 bytes (before any overhead).
- The client library has a global protobuf encode/decode buffer:
  - `PB_BUFSIZE = 512` in `mt_protocol.cpp`

### Receive path / callbacks

In `Keyboard/.pio/libdeps/teensy41/Meshtastic/src/mt_protocol.cpp`:

- Incoming protobufs are decoded in `mt_loop() -> mt_protocol_check_packet() -> handle_packet()`.
- When `FromRadio.packet` arrives, `handle_mesh_packet()` dispatches:
  - `TEXT_MESSAGE_APP` -> `text_message_callback(from, to, channel, payloadBytesAsChar*)`
  - Any other decoded portnum -> `portnum_callback(from, to, channel, portnum, &payload)`
  - Encrypted payload -> `encrypted_callback(...)`

Observation: `FromRadio.queueStatus` and `FromRadio.clientNotification` exist in protobufs, but the Arduino client library currently does not expose callbacks for them (it only logs queueStatus via debug).

This matters because `QueueStatus.mesh_packet_id` could be a useful hook for low-level send/queue confirmation if we choose to integrate it.

### Critical constraint: global protobuf buffer (thread-safety + reentrancy)

In `mt_protocol.cpp`:

- The library uses a single global `pb_buf` for both encode and decode, and a global `pb_size` for the receive FIFO.
- `_mt_send_toRadio(...)` **overwrites** `pb_buf` and sets `pb_size = 0`.

Implications:

- The library is not thread-safe: sending from one thread while `mt_loop()` decodes in another can corrupt state.
- Sending from within a Meshtastic receive callback (which runs inside `mt_loop()`) can drop/corrupt any remaining bytes in the input buffer.

Therefore: all Meshtastic send operations should be serialized on one thread, and sends should not occur inside receive callbacks. Queue outbound frames and send them after `mt_loop()` returns.

## Protobuf Fields Relevant To Reliability

From `Keyboard/.pio/libdeps/teensy41/Meshtastic/protobufs/meshtastic/mesh.proto`:

- `Data.request_id` / `Data.reply_id` exist (mostly for routing/response semantics)
- `MeshPacket.want_ack` exists
- `Routing` message exists on `PortNum.ROUTING_APP`, but the `.proto` does not define a simple application-visible `Ack` message.

Conclusion: app-layer ACKs for multi-chunk transfer should be implemented as our own message types carried in `PortNum.PRIVATE_APP` (or another portnum >= 256).

## Candidate Architecture

### Transport wrapper

Introduce a small module/class (example naming):

- `RakTransport` (or `RakReliableTransport`)
  - `send(dest, bytes, opts) -> handle/id`
  - `poll()` or `tick(now)` called from `RakManager::listenerThread`
  - `onReceive(from, payloadBytes)` invoked from `portnum_callback`
  - Emits:
    - `onPayloadComplete(from, bytes)`
    - `onProgress(id, sentChunks, ackedChunks)`
    - `onError(id, reason)`

## Current Implementation (in this branch)

Implemented a first working version in:

- `Keyboard/include/RakTransport.h`
- `Keyboard/src/RakTransport.cpp`

Integration points:

- `RakManager::portnum_callback(...)` enqueues `PortNum::PRIVATE_APP` payloads into `RakTransport` (no sends in callback).
- `RakManager::listenerThread(...)` calls `mt_loop(now)` then `transport_.tick(now)` to process RX/TX.
- `RakManager::handleAiCompletion(...)` and the heartbeat no longer call `mt_send_text()` directly; they enqueue text sends so Meshtastic sends are serialized.

Thread-safety fix:

- `RakManager::mastersAddresses` is now guarded by `mastersMutex_`, and read operations use snapshots to avoid iterator invalidation/data races.

### Reliable Transfer Strategy (v1)

Stop-and-wait per chunk (single active transfer at a time):

- Sender sends chunk `i`, waits for app-level ACK `(msgId, chunkIndex=i)`.
- If ACK not received within `ackTimeoutMs`, resend chunk `i`.
- Abort after `maxRetriesPerChunk`.
- Receiver ACKs each received chunk (idempotent ACK for duplicates).
- Receiver reassembles chunks and emits `payloadCompleteCb_` when all chunks are present.

### Frame Format (PRIVATE_APP, binary, v1)

All frames are carried in `MeshPacket.decoded.payload` with `decoded.portnum = PRIVATE_APP`.

- Max decoded payload bytes (nanopb): 233 (`PB_BYTES_ARRAY_T(233)` in generated `mesh.pb.h`).
- Header is 16 bytes, little-endian integers.
- Therefore max chunk data per packet is `233 - 16 = 217` bytes.

Header layout:

- `magic[2]` = `0x52 0x4B` (ASCII `R K`)
- `version` = `1`
- `type` = `1` DATA, `2` ACK
- `msgId` (u32)
- `chunkIndex` (u16)
- `chunkCount` (u16)
- `totalLen` (u32)

DATA frame:

- Header + `chunkData[...]` (0..217 bytes)

ACK frame:

- Header only (no trailing payload)

### Important Meshtastic-arduino Limitation Mitigation

The client library clears its RX FIFO (`pb_size = 0`) after encoding/sending (`_mt_send_toRadio(...)`). If we send while `pb_size != 0`, we can discard partially-received bytes that were already consumed from the serial stream (leading to a dropped/corrupted incoming protobuf frame).

Mitigation implemented:

- `RakTransport` checks the internal `pb_size` global and only sends when `pb_size == 0`.
- This is still an internal coupling to Meshtastic-arduino (`pb_size` and `_mt_send_toRadio` are not public API).

### Fragmentation frame format (binary)

Use `PortNum.PRIVATE_APP` and put a small header in `meshtastic_Data.payload`.

Sketch:

- `magic` (1-2 bytes) + `version` (1 byte)
- `type` (DATA/ACK/NACK)
- `msg_id` (u32)
- `chunk_index` (u16)
- `chunk_count` (u16)
- `total_len` (u32) optional
- `crc32` (u32) for whole payload (or per-chunk CRC)
- `payload_bytes[...]`

### ACK strategy

Because LoRa is slow, ACK traffic must be small:

- ACK could carry:
  - `msg_id`
  - `highest_contiguous_chunk` (u16)
  - plus a small bitset for the next N chunks (selective ACK)
  - or a list of missing chunk indices (NACK list) if loss is low

Sender policy:

- small window (e.g. 2-5 chunks in flight)
- timeout per window
- resend only missing chunks
- abort after max retries

### Sending binary payloads (library limitation)

Because the Arduino client library exposes only `mt_send_text()`, we likely need one of:

1. Declare/use the internal `_mt_send_toRadio(meshtastic_ToRadio)` symbol directly (it is not `static` in `mt_protocol.cpp`), and build our own `MeshPacket` with `decoded.portnum = PRIVATE_APP` and bounded `payload.size`.
2. Vendor/patch the Meshtastic-arduino library to expose `mt_send_data(...)` officially.

Option (1) is quickest but relies on an internal API (potential upstream breakage).
Option (2) is cleaner but larger change (maintaining a fork/vendored lib).

## Open Questions / Things To Verify Online

- What does the Meshtastic firmware actually emit back to clients for `want_ack`? Is it visible over the serial/wifi protobuf interface (e.g. via `QueueStatus`, `ClientNotification`, or a `ROUTING_APP` packet)?
- Best practice portnum selection for private protocols (likely `PortNum.PRIVATE_APP = 256`).
- Max practical payload per packet over common LoRa presets (233 bytes is the protobuf max, but radio MTU might be smaller for certain settings).

  ## Online Docs Cross-Check (notes)

- Meshtastic "Client API (Serial/TCP/BLE)" docs describe a 4-byte framing header for streaming transports:
  - START1 `0x94`, START2 `0xC3`, then 16-bit payload length
  - Receiver treats length >512 as corrupted and resynchronizes
  - Matches this client library implementation (`MT_MAGIC_0/1` and `PB_BUFSIZE=512`)
- Meshtastic firmware Phone API surfaces:
  - `FromRadio.queueStatus` (includes `mesh_packet_id`, free/maxlen/res)
  - `FromRadio.clientNotification` (includes optional `reply_id` and human-readable message)
  - The Arduino client library currently parses some of these but does not provide callbacks for them.

## Next Steps

1. Add a small example call site (or CLI command) that uses `RakManager::sendReliable(...)` / `sendReliableToMasters(...)` for an actual payload.
2. Decide whether to extend beyond stop-and-wait:
   - small sliding window
   - selective ACK bitset
3. Consider integrity:
   - optional CRC32 for whole payload (in header or final frame)
4. If upstream Meshtastic-arduino changes break internal symbols:
   - vendor/fork the library to expose a supported `mt_send_data(...)` and a safe RX/TX buffer model.
