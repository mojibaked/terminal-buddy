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

## Transcription Backends

The current app supports both the existing OpenAI backend and a local Snapdragon NPU path through the sibling `npu-stt-c` library.

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
# Optional overrides if you don't want to use the sibling npu-stt-c artifact layout:
# TB_NPU_RUNTIME_DIR=C:\path\to\runtime\windows-arm64
# TB_NPU_PACKAGE_DIR=C:\path\to\models\whisper-base-en-qnn
# TB_NPU_TOKENIZER_VOCAB_PATH=C:\path\to\models\whisper-base-assets\vocab_by_id.txt
# TB_NPU_MEL_FILTERS_PATH=C:\path\to\models\whisper-base-assets\mel_filters_201x80_f32.bin
```

`terminal-buddy` now consumes the sibling `../npu-stt-c` library and, by default, expects its staged artifact layout:

```text
../npu-stt-c/runtime/windows-arm64/onnxruntime.dll
../npu-stt-c/runtime/windows-arm64/QnnHtp.dll
../npu-stt-c/models/whisper-base-en-qnn/HfWhisperEncoder.onnx
../npu-stt-c/models/whisper-base-en-qnn/HfWhisperDecoder.onnx
../npu-stt-c/models/whisper-base-assets/vocab_by_id.txt
../npu-stt-c/models/whisper-base-assets/mel_filters_201x80_f32.bin
```

Default sibling model mapping:

- `whisper_tiny_en` -> `../npu-stt-c/models/whisper-tiny-en-qnn`
- `whisper_base_en` -> `../npu-stt-c/models/whisper-base-en-qnn`
- `whisper_small_en` -> `../npu-stt-c/models/whisper-small-en-qnn`
- `whisper_large_v3_turbo` -> `../npu-stt-c/build/local-models/whisper-large-v3-turbo-root/whisper-large-v3-turbo-qnn`

`Large V3 Turbo` also expects the sibling release-asset staging path for its `128`-bin mel filters:

```text
../npu-stt-c/build/local-models/whisper-large-v3-turbo-root/whisper-large-v3-assets/vocab_by_id.txt
../npu-stt-c/build/local-models/whisper-large-v3-turbo-root/whisper-large-v3-assets/mel_filters_201x128_f32.bin
```

If those paths do not exist on your machine, keep using `Tiny`, `Base`, or `Small`, or set the NPU override env vars explicitly.

To verify the staged backend can initialize and transcribe through `npu-stt-c`, run:

```powershell
@'
TB_TRANSCRIPTION_BACKEND=npu
'@ | Set-Content build\probe-real-npu.env

.\build\Debug\transcription-probe.exe .\build\probe-real-npu.env --audio .\build\Recording-probe.wav
```

The probe prints the resolved runtime/package paths, runs a real transcription, and reports the same timing block the app shows in the UI.

## Next steps

1. Replace the placeholder window with a draggable always-on-top touch button.
2. Add tray support and a tiny mode menu.
3. Capture microphone audio.
4. Stream realtime transcription.
5. Paste finalized text into Windows Terminal.

More detail lives in [docs/roadmap.md](docs/roadmap.md).
