# terminal-buddy

Minimal native scaffold for a Windows-first dictation companion that targets Windows Terminal.

## Current scope

- `C` project with `CMake`
- `SDL3` via `FetchContent`
- Tiny starter window that hints at the eventual floating control + transcript overlay

## Build

```powershell
cmake -S . -B build
cmake --build build
```

Then run:

```powershell
.\build\Debug\terminal-buddy.exe
```

For single-config generators such as Ninja, the executable is usually at:

```powershell
.\build\terminal-buddy.exe
```

## Next steps

1. Replace the placeholder window with a draggable always-on-top touch button.
2. Add tray support and a tiny mode menu.
3. Capture microphone audio.
4. Stream realtime transcription.
5. Paste finalized text into Windows Terminal.

More detail lives in [docs/roadmap.md](docs/roadmap.md).
