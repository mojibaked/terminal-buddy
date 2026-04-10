# Managed Terminal Integration

## Goal

Bring the `winterm` terminal-state pieces directly into `terminal-buddy` so
Buddy can own terminal state instead of depending on external terminal apps.

This is the path for:

- managed shell targets
- authoritative terminal snapshots
- reliable summaries from terminal state
- later speech policy and event extraction

## What To Reuse From `winterm`

The useful pieces are much smaller than the full `winterm` app.

Absorb:

- Windows `ConPTY` ownership
- `ghostty-vt` terminal model
- formatter-based plain-text snapshot export
- later: speech policy and event extraction

Do not absorb:

- SDL renderer stack
- terminal view/UI code
- overlay rendering and selection UX

## Minimal Internal Stack

The first internal backend should be:

1. `ConPTY`
   - spawn shell
   - read output
   - write input
   - resize
2. `GhosttyTerminal`
   - consume VT bytes
   - track the terminal model
   - surface title changes
3. snapshot formatter
   - export terminal contents as plain text
   - provide state to Buddy routing and summaries

That is enough for a real managed shell target.

## Initial Landing In `terminal-buddy`

The current extraction is intentionally small:

- [managed_terminal.h](../src/managed_terminal.h)
- [managed_terminal_win32.c](../src/managed_terminal_win32.c)
- [managed_terminal_probe.c](../src/managed_terminal_probe.c)

It provides:

- shell startup through `ConPTY`
- `ghostty-vt` ownership in-process
- VT pumping into a managed terminal model
- title updates
- plain-text terminal snapshot export

This is behind the CMake option:

- `TERMINAL_BUDDY_ENABLE_MANAGED_TERMINAL_BACKEND=ON`

The verification target is:

- `managed-terminal-probe`

## Next Steps

1. Enroll each managed shell as a first-class Buddy target instead of an
   observed window target.
2. Feed managed shell snapshots into the sidecar target-state adapter.
3. Route shell commands to the managed terminal transport instead of paste into
   external windows.
4. Add speech-policy extraction from the same output pump.
5. Reuse observed external terminals only as a fallback path.

## Build Note

The Ghostty terminal backend currently requires fetching `ghostty` and, in
practice, a working `zig` toolchain. That is why the managed-terminal backend is
opt-in for now instead of enabled in the default build.
