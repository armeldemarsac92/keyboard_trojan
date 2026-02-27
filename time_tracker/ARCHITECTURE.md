# Time Tracker Architecture

## Layout
- `time_tracker.c`: thin application entrypoint (`main`) and orchestration.
- `include/time_tracker/config.h`: protocol and runtime constants.
- `include/time_tracker/types.h`: shared DTO/types (`work_item_t`, poll state, app options).
- `include/time_tracker/order_queue.h` + `src/time_tracker/order_queue.c`: thread-safe producer/consumer queue.
- `include/time_tracker/worker.h` + `src/time_tracker/worker.c`: worker thread and task dispatch.
- `include/time_tracker/runtime.h` + `src/time_tracker/runtime.c`: Windows/runtime helpers (logging, UTF conversion, error logging).
- `include/time_tracker/hid_channel.h` + `src/time_tracker/hid_channel.c`: HID transport (polling, report framing, device open).
- `vendor/hidapi/`: vendored HIDAPI backend (`hid.c` + related `hidapi_*` headers/sources).

## Message Flow
1. Main loop polls HID feature reports:
- `0x02` -> shell-command work item
- `0x03` -> worker-order work item
2. Incoming payload is deduplicated per report type and enqueued.
3. Worker thread dequeues and dispatches one item at a time.
4. Shell command path is allowlisted (`cmd power on`, `cmd power off`).

## Build
- `make` builds `output/build/time_tracker.exe`
- `make package` generates script artifacts in `output/artifacts/`
- `make clean` removes `output/`
