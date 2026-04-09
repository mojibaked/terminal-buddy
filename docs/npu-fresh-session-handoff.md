# NPU Fresh Session Handoff

Last updated: 2026-04-07

## Goal

Provide real local speech-to-text for `terminal-buddy` on Snapdragon Windows hardware using Whisper on the Qualcomm NPU, while keeping the existing OpenAI path as fallback.

## Current Reality

The repo is past the integration milestone.

What works now:

- The app builds.
- The OpenAI transcription backend still works.
- `TB_TRANSCRIPTION_BACKEND=npu` is wired through the backend seam.
- The Qualcomm-exported Whisper encoder and decoder load through ONNX Runtime + QNN.
- Native Whisper preprocessing now exists in C.
- Native tokenizer and detokenizer assets are staged locally.
- A real greedy decode loop now runs on the exported decoder.
- `tb_transcription_backend_npu_execute` now performs real local inference instead of returning a placeholder error.
- `transcription-probe --audio` now runs end-to-end transcription on a WAV file.
- The added repo recording was validated through the NPU path after WAV conversion and produced:

```text
Hello, this is a sample voice recording for Codex.
```

What still needs follow-up:

- Validate the full in-app capture flow end-to-end, not just the probe path.
- Test longer utterances and chunk stitching behavior.
- Measure latency and memory on real target hardware instead of focusing only on correctness.
- Decide whether tokenizer and mel-filter assets need packaging changes for release builds.

## Locked MVP Decisions

These decisions are already made and should be preserved unless the user explicitly changes scope:

1. Offline only.
2. One utterance at a time.
3. English-only `whisper_base.en`.
4. Final text only.
5. No timestamps.
6. Greedy decode only.
7. No streaming partials.
8. Keep the existing UI flow and swap only backend internals.

Definitions:

- An utterance is one captured recording segment.
- Timestamps are time offsets attached to transcript segments or words.
- Greedy decode means selecting the highest-probability next token at each decoder step.

## Exported Model Package

The Qualcomm AI Hub export shape is:

```text
models/whisper_qnn/HfWhisperEncoder.onnx
models/whisper_qnn/HfWhisperEncoder_qairt_context.bin
models/whisper_qnn/HfWhisperDecoder.onnx
models/whisper_qnn/HfWhisperDecoder_qairt_context.bin
```

This matters because earlier assumptions in the project treated `precompiled_qnn_onnx` as one wrapper ONNX plus one `.bin`. That is not how this `whisper_base` export arrived.

## Runtime Package

The ONNX Runtime QNN Windows ARM64 runtime is staged under:

```text
runtime/npu/
```

Important files:

- `onnxruntime.dll`
- `onnxruntime_providers_shared.dll`
- `onnxruntime_providers_qnn.dll`
- `QnnHtp.dll`
- `QnnHtpPrepare.dll`
- `QnnSystem.dll`
- `QnnHtpV73Stub.dll`
- `QnnHtpV81Stub.dll`

Source of those files:

- `scripts/fetch-onnxruntime-qnn.ps1`

The script currently pulls `Microsoft.ML.OnnxRuntime.QNN` version `1.24.4`.

## Whisper Assets

The repo now also depends on native Whisper asset files staged under:

```text
models/whisper_base_assets/vocab_by_id.txt
models/whisper_base_assets/mel_filters_201x80_f32.bin
```

These are consumed by the native NPU backend for token decode and feature extraction.

## Files Added Or Updated For NPU Work

Core backend seam:

- `src/transcription_backend.h`
- `src/transcription_backend.c`
- `src/transcription_backend_openai_win32.c`
- `src/transcription_backend_npu_win32.c`

Native ORT + QNN runtime path:

- `src/transcription_backend_npu_ort_win32.c`
- `src/transcription_backend_npu_ort_win32.h`

Native Whisper frontend:

- `src/transcription_backend_npu_whisper.c`
- `src/transcription_backend_npu_whisper.h`

Probe and test harness:

- `src/transcription_probe.c`

Build/config/docs:

- `CMakeLists.txt`
- `README.md`
- `docs/npu-feasibility.md`
- `.gitignore`
- `scripts/fetch-onnxruntime-qnn.ps1`

Unrelated but important build fix:

- `CMakeLists.txt` no longer links the unused `SDL3_image` dependency that was breaking ARM64 builds.
- `third_party/clay/clay_renderer_SDL3.c` had the stray `SDL3_image` include removed.

## Important Verified Commands

Build:

```powershell
cmake -S . -B build
cmake --build build
```

Probe staged NPU package:

```powershell
@'
TB_TRANSCRIPTION_BACKEND=npu
'@ | Set-Content build\probe-real-npu.env

.\build\Debug\transcription-probe.exe .\build\probe-real-npu.env
```

Run the native QNN smoke test:

```powershell
.\build\Debug\transcription-probe.exe .\build\probe-real-npu.env --smoke
```

Convert the repo recording to probe-friendly WAV:

```powershell
& .\.venv-qai\Lib\site-packages\imageio_ffmpeg\binaries\ffmpeg-win-x86_64-v7.1.exe `
  -y `
  -i .\Recording.m4a `
  -ac 1 `
  -ar 24000 `
  -c:a pcm_f32le `
  .\build\Recording-probe.wav
```

Run real end-to-end local transcription:

```powershell
.\build\Debug\transcription-probe.exe .\build\probe-real-npu.env --audio .\build\Recording-probe.wav
```

Expected result:

- `ready=1`
- `status=NPU BACKEND READY`
- `transcribe=1`
- `transcript=Hello, this is a sample voice recording for Codex.`

## QNN Warning Caveat

The smoke and real runs emit QNN warnings similar to:

- `MapDmaData Failed to register DMA data mapping to RPCMEM`
- file-mapping retry warnings in `LoadCachedQnnContextFromBuffer`

Current interpretation:

- not a blocker for the MVP path
- worth tracking during later optimization and packaging work
- do not treat those warnings as proof that inference failed

The probe also creates a local `_qnn.log`, which is ignored in `.gitignore`.

## Current NPU Backend Status

Two things are now real:

1. Probe / smoke infrastructure
- validated against the actual exported Qualcomm package

2. Actual STT path
- implemented
- exercised through `tb_transcription_execute`
- produces transcript text from real audio

The current implementation does:

- mono float audio input
- resampling to 16 kHz
- native Whisper log-mel feature extraction
- encoder pass once per chunk
- greedy decoder loop with self-cache reuse
- English forced prefix
- no timestamps
- final text concatenation across chunks

## Export Provenance

The Qualcomm export was produced from `whisper_base` targeting `precompiled_qnn_onnx`.

Useful facts already learned:

- Qualcomm AI Hub tooling on Windows is happier with x64 Python for export, not ARM64 Python.
- The repo already has the exported assets, so future sessions should not spend time re-solving export tooling unless the user wants to regenerate the models.
- Qualcomm auth/token handling should not be repeated in chat. If needed again, use the local client config and rotate credentials normally outside the transcript.

## Exact Next Engineering Tasks

Focus on validation and polish, not basic integration:

1. Validate the interactive app recording flow end-to-end with `TB_TRANSCRIPTION_BACKEND=npu`.
2. Test longer recordings to check chunk quality and spacing between chunks.
3. Measure latency on target hardware and decide whether session reuse or caching changes are needed.
4. Make sure tokenizer and mel-filter assets are packaged anywhere the app binary is expected to run.
5. If QNN warning noise becomes a support problem, investigate whether provider configuration can suppress or avoid the file-mapping fallback path.

## Known Constraints

- This is Windows-only code right now.
- The probe audio path expects WAV input; the repo recording was converted from `.m4a` before transcription.
- The smoke helper relies on ONNX Runtime headers extracted into `build/nuget-cache/.../build/native/include`.
- `CMakeLists.txt` conditionally enables that include path if the headers exist.
- If `terminal-buddy.exe` is running during rebuilds, MSVC may fail with `LNK1168` because the executable is locked.

## Good Starting Files For The Next Session

Start here:

- `src/transcription_backend_npu_ort_win32.c`
- `src/transcription_backend_npu_whisper.c`
- `src/transcription_backend_npu_win32.c`
- `src/transcription_probe.c`
- `src/main.c`

Read for context:

- `docs/npu-feasibility.md`
- `README.md`

## What To Avoid Re-Doing

- Do not revisit “is NPU feasible?” unless the user asks.
- Do not revisit Qualcomm export troubleshooting unless the user wants a new model export.
- Do not re-implement the Whisper frontend or decode loop unless there is a concrete bug.
- Do not add timestamps or beam search before validating the current offline greedy path in the app.

## Single-Sentence Summary

The repo now has working offline Whisper transcription running through ONNX Runtime + QNN on the staged Qualcomm package, and the next session should focus on validation, packaging, and performance instead of core NPU integration.
