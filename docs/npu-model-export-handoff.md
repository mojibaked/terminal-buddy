# NPU Model Export — Handoff Notes (2026-04-07)

## Goal
Export `whisper_base` from Qualcomm AI Hub as precompiled QNN ONNX for on-device NPU inference on Snapdragon X Elite (Surface Pro).

## What's Done
- Qualcomm AI Hub account created, API token configured (`~/.qai_hub/client.ini`)
- Confirmed the `qai-hub-models` package (v0.49.1) has these Whisper variants: `whisper_tiny`, `whisper_base`, `whisper_small`, `whisper_small_quantized`, `whisper_large_v3_turbo`
- The export script is at `qai_hub_models.models.whisper_base.export` and supports `--target-runtime precompiled_qnn_onnx`
- The export is a **cloud compile** — it uploads the model to Qualcomm's servers and downloads the compiled artifacts. It does NOT need to run on ARM natively.

## Blockers: ARM64 Python Doesn't Work for This

We installed ARM64 Python 3.13 but hit two dependency walls:

1. **onnxruntime**: `qai-hub-models` requires `>=1.19,<1.23`. The first ARM64 Windows wheel is 1.24.2. No ARM64 builds exist in the required range.
2. **PyTorch**: No ARM64 Windows wheels exist at all (as of April 2026).

These are **export-time** dependencies only (tracing, ONNX conversion before cloud upload). The actual on-device inference in the C app won't use Python at all.

## Recommended Path: Use x86 Python for Export

The export step runs fine under x86 Python emulation on ARM — it's just Python scripting + network calls to Qualcomm's cloud. The one issue we hit with x86 Python was:

- `sounddevice` (imported at module level in `qai_hub_models.models._shared.hf_whisper.app`) tries to load an ARM64 PortAudio DLL, which x86 Python can't load.
- This import happens in `whisper_base/__init__.py` (line 6) before the export script even runs.
- **Fix options:**
  - Patch the `__init__.py` to make the `sounddevice` import lazy/optional (it's only needed for the live demo, not export)
  - Or run the export function directly via a custom Python script that avoids importing `__init__.py`
  - Or install an x86-compatible `sounddevice` / PortAudio

## Current Python State
- x86 Microsoft Store Python 3.13: **uninstalled**
- ARM64 Python 3.13.12: **installed** at `C:\Users\ether\AppData\Local\Programs\Python\Python313-arm64\python.exe`, accessible via `py` command
- ARM64 Python has `onnxruntime 1.24.4` and `numpy` installed (should uninstall if switching back to x86 approach)
- `qai-hub` and `qai-hub-models` were uninstalled from ARM64 Python
- The API token is still valid in `~/.qai_hub/client.ini` (user plans to rotate it)

## Export Command (Once Dependencies Work)
```
python -m qai_hub_models.models.whisper_base.export --target-runtime precompiled_qnn_onnx
```
Output goes to `./export_assets/` — move the `.onnx` + `.bin` files into `models/whisper_qnn/`.

## Market Research Summary
- **No third-party app ships local Whisper-on-NPU dictation on Windows** as of April 2026
- Windows Voice Typing (Win+H) does NOT use the NPU — it's Azure cloud or a weak CPU offline model
- Live Captions does on-device STT via NPU but has no API (display-only)
- Dragon NaturallySpeaking is discontinued
- The NPU ecosystem is mostly Microsoft first-party features (Studio Effects, Live Captions, Recall, Cocreator)
- terminal-buddy would be among the first apps doing this
