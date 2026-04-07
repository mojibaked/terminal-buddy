# Roadmap

## Goal

Build a lightweight Windows 11 assistant focused on:

- touch-friendly floating record button
- low-latency speech-to-text
- mode-aware cleanup for terminal and coding workflows
- final text insertion into Windows Terminal

## Phase 0: Repo Scaffold

- `CMake` project bootstrapped
- `SDL3` fetched from upstream
- minimal render loop in place

Exit criteria:

- project configures on a clean machine
- starter app builds and opens a window

## Phase 1: Floating Control

- replace the placeholder shell with a small draggable button
- keep the window borderless and always on top
- add visual state for idle, listening, and processing
- support mouse first, then verify touch behavior on Surface hardware

Exit criteria:

- button can be repositioned
- button does not feel awkward to activate from touch

## Phase 2: Tray + Modes

- add tray icon and context menu
- expose `standard`, `coding`, and `terminal` modes
- persist window position and selected mode

Exit criteria:

- app can stay backgrounded
- mode changes survive restart

## Phase 3: Audio + Realtime STT

- capture mic input on Windows
- stream audio to a realtime transcription session
- display partial text in the overlay
- finalize text on silence or tap

Exit criteria:

- partial transcript updates feel responsive
- failures are visible and recoverable

## Phase 4: Windows Terminal Injection

- detect when Windows Terminal is foregrounded
- insert finalized text reliably
- preserve a fallback path if direct input injection is flaky

Exit criteria:

- dictated text arrives in the active Windows Terminal session
- insertion is reliable enough for daily use

## Phase 5: Cleanup Layer

- add per-mode transcript cleanup rules
- keep shell commands, flags, filenames, and code tokens intact
- avoid over-editing the user's phrasing

Exit criteria:

- `terminal` mode preserves shell syntax
- `coding` mode preserves technical vocabulary

## Notes

- Keep the binary small and the idle CPU near zero.
- Prefer event-driven logic over polling where practical.
- Treat touch behavior and latency as first-class constraints, not polish tasks.
