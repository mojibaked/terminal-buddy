# terminal-buddy

Minimal native scaffold for a Windows-first dictation companion that now owns
its own terminal window as the manual-override surface.

The terminal window is still borrowed from the latest `winterm` UI stack, but
it now runs in-process inside `terminal-buddy.exe` instead of being launched as
a separate companion app.

## Current scope

- `C` project with `CMake`
- `SDL3` via `FetchContent`
- Tiny starter window that hints at the eventual floating control + transcript overlay
- Optional in-process terminal window plus an optional standalone terminal
  companion executable, both using the sibling `winterm` + `strata` stack

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

To enable the owned terminal window path, configure with:

```powershell
cmake -S . -B build -DTERMINAL_BUDDY_BUILD_TERMINAL_UI=ON
cmake --build build --target terminal-buddy
```

That same flag also keeps the standalone companion target available:

```powershell
cmake --build build --target terminal-buddy-terminal
```

Then run:

```powershell
.\build\Debug\terminal-buddy-terminal.exe
```

When built with `TERMINAL_BUDDY_BUILD_TERMINAL_UI=ON`, the Buddy widget exposes
a left-side `term` button. That button now opens or refocuses Buddy's owned
terminal window inside the same `terminal-buddy.exe` process and switches Buddy
into terminal dictation mode.

If the owned window path is unavailable at runtime, Buddy still falls back to
launching the standalone `terminal-buddy-terminal.exe` companion.

What to test in the owned terminal window:

- open a few tabs and confirm tab switching / close behavior works
- run normal shell commands directly when Buddy would otherwise get stuck
- keep the terminal window open beside the floating Buddy app as a manual override
- confirm the `term` button does not spawn a separate `terminal-buddy-terminal.exe`
  process when the owned window path is built in

Optional debug hooks for the borrowed `winterm` UI:

- `WINTERM_STRATA_MEMORY_LOG_PATH=C:\path\to\memory-log.csv`
- `WINTERM_SPEECH_LOG_PATH=C:\path\to\speech-log.jsonl`
- `WINTERM_SPEECH_DEBUG=1` to mirror speech events to stderr
- `WINTERM_AUTOMATION_INBOX=C:\path\to\inbox.txt` to inject text into the active tab

## Transcription Backends

The current app supports both the existing OpenAI backend and a local Snapdragon NPU path through the `stt` library inside the sibling [NPU Voice C](../npu-voice-c) repo.

At runtime, the system tray now lets you switch:
- backend: `OpenAI` or `NPU`
- NPU model: `Tiny`, `Base`, `Small`, or `Large V3 Turbo`

In `.env/dev.env`:

```powershell
TB_TRANSCRIPTION_BACKEND=openai
OPENAI_API_KEY=...
OPENAI_TRANSCRIBE_MODEL=gpt-4o-transcribe
```

For local NPU inference:

```powershell
TB_TRANSCRIPTION_BACKEND=npu
TB_TRANSCRIPTION_MODEL=whisper_small_en
# Optional: evict cached ORT/QNN sessions after 60s idle. Set 0 to keep them forever.
# TB_NPU_CACHE_IDLE_MS=60000
# Optional override if your staged STT assets are not under ../npu-voice-assets/stt:
# TB_NPU_INSTALL_ROOT=C:\path\to\npu-stt-assets
# Optional per-file overrides if you don't want to use the install-root layout:
# TB_NPU_RUNTIME_DIR=C:\path\to\runtime\windows-arm64
# TB_NPU_PACKAGE_DIR=C:\path\to\models\whisper-base-en-qnn
# TB_NPU_TOKENIZER_VOCAB_PATH=C:\path\to\models\whisper-base-assets\vocab_by_id.txt
# TB_NPU_MEL_FILTERS_PATH=C:\path\to\models\whisper-base-assets\mel_filters_201x80_f32.bin
```

`terminal-buddy` now builds against the sibling `../npu-voice-c` source tree and, by default, uses `../npu-voice-assets/stt` as the STT install-root-style artifact folder:

```text
../npu-voice-assets/stt/runtime/windows-arm64/onnxruntime.dll
../npu-voice-assets/stt/runtime/windows-arm64/QnnHtp.dll
../npu-voice-assets/stt/models/whisper-base-en-qnn/HfWhisperEncoder.onnx
../npu-voice-assets/stt/models/whisper-base-en-qnn/HfWhisperDecoder.onnx
../npu-voice-assets/stt/models/whisper-base-assets/vocab_by_id.txt
../npu-voice-assets/stt/models/whisper-base-assets/mel_filters_201x80_f32.bin
```

The STT library now owns the default model-to-package and asset mapping. `terminal-buddy` only passes the install root unless you explicitly override individual paths.

To verify the staged backend can initialize and transcribe through the ported STT library, a probe-only build is enough:

```powershell
cmake -S . -B build-port-proof -G Ninja `
  -DTERMINAL_BUDDY_BUILD_APP=OFF `
  -DTERMINAL_BUDDY_BUILD_TEXT_ENGINE_PROBE=OFF `
  -DTERMINAL_BUDDY_BUILD_TRANSCRIPTION_PROBE=ON
cmake --build build-port-proof --target transcription-probe
```

Then run:

```powershell
@'
TB_TRANSCRIPTION_BACKEND=npu
'@ | Set-Content build\probe-real-npu.env

.\build-port-proof\transcription-probe.exe .\build\probe-real-npu.env --smoke --audio ..\npu-voice-c\stt\tests\golden.wav
```

The probe prints the resolved install root, runtime/package paths, runs a real transcription, and reports the same timing block the app shows in the UI.

## Next steps

The product direction is now broader than dictation-only terminal insertion.

Key docs:

- [docs/product-architecture.md](docs/product-architecture.md)
- [docs/roadmap.md](docs/roadmap.md)
- [docs/managed-terminal-integration.md](docs/managed-terminal-integration.md)
