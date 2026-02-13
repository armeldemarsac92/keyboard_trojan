# Keyboard Typing Latency Notes

Branch: `fix/top5-hardening`

## Symptom

Intermittent latency between physical key press and seeing the character on the host.

## Likely Causes In This Codebase

1. **Blocking work in the main `loop()`** delays `myusb.Task()`.
   - Physical key events from the USBHost keyboards are dispatched from `myusb.Task()`.
   - If `loop()` spends noticeable time in other work (ex: SQLite transactions), the USBHost stack is serviced less often, which shows up as input lag.

2. **Locks on the hot key-forwarding path**
   - Wrapping every `Keyboard.press()/release()` in a `Threads::Mutex` adds overhead and can introduce jitter, even when uncontended.

3. **USB serial logging contention**
   - Heavy `Serial` logging can contend on the same USB bus as the HID keyboard endpoint and can also block threads if the host is not draining logs fast enough.

## Fix Strategy Implemented

1. **Keep `loop()` primarily focused on USBHost servicing**
   - Move DB flushing (`DatabaseManager::processQueue()`) to a dedicated writer thread.
   - `loop()` now primarily runs `myusb.Task()` plus a small keyboard-injection tick.

2. **Avoid per-keystroke mutexes**
   - Remove `usbKeyboardMutex` usage from the physical key handlers.
   - Ensure cross-thread injected typing does not call `Keyboard.*` directly.

3. **Defer radio-driven typing to the main loop and chunk it**
   - `HostKeyboard` now provides `enqueueTypeText()` + `tick()` and types only a small number of characters per tick.
   - This prevents long blocking sequences from starving `myusb.Task()`.

## Additional Notes

- `HostKeyboard` no longer calls `Keyboard.releaseAll()` to avoid interfering with physically-held modifiers if a remote typing job happens while someone is typing locally.
