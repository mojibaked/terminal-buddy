# NPU-Accelerated Whisper STT on Snapdragon X Elite (Surface Pro 11)

## Model Options & NPU Latency (QNN Context Binary, X Elite)

| Model               | Encoder | Decoder/token | ~Total (30s clip) | Size    |
|---------------------|---------|---------------|--------------------|---------|
| whisper_tiny        | 18ms    | 1.9ms         | ~400ms             | 145 MB  |
| whisper_base        | 37ms    | 3.3ms         | ~700ms             | 278 MB  |
| whisper_small       | 115ms   | 10ms          | ~2.1s              | 924 MB  |
| whisper_large_v3_turbo | 580ms | 8.1ms        | ~2.2s              | larger  |

**whisper_base is the sweet spot** for short clips (5-30s) — sub-second on NPU.
English-only variants (`*.en`) are faster and more accurate for coding agent dictation.

For comparison, **whisper.cpp on CPU** (ARM NEON, no NPU) is roughly **10-20x slower**: ~5-6s for a 30s clip with whisper_base.

## Deployment Architecture (C/C++ app, no Python at runtime)

```
your_app.exe
whisper_encoder.onnx    (or .bin QNN context binary)
whisper_decoder.onnx
onnxruntime.dll         (built with QNN EP)
```

**Python is only needed at dev time** for the one-time model export/compile step.
At runtime, it's pure C + precompiled model artifacts + ONNX Runtime.

## Two C/C++ Runtime Options

### 1. ONNX Runtime C API + QNN Execution Provider (recommended)
- Well-documented, portable, stable C API
- QNN EP supported on Windows ARM64
- Docs: https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html
- Build: `build.bat --arm64 --use_qnn --qnn_home=[QNN_SDK path]`

```c
#include <onnxruntime_c_api.h>

OrtSessionOptions* opts;
OrtSessionOptionsAppendExecutionProvider(opts, "QNN", qnn_options, num_options);
OrtSession* session;
OrtCreateSession(env, "whisper_encoder.onnx", opts, &session);
```

### 2. QAI AppBuilder (Qualcomm's C++ wrapper)
- Simplified C++ lib for Windows on Snapdragon
- Prebuilt `libappbuilder.dll` + `.lib` for `win_arm64`
- Repo: https://github.com/quic/ai-engine-direct-helper

## One-Time Model Compilation Pipeline (dev machine, uses Python)

1. `pip install "qai-hub-models[whisper-base]"`
2. Export Whisper to ONNX (or grab pre-exported from AI Hub)
3. Compile/quantize for QNN target:
   ```bash
   python -m qai_hub_models.models.whisper_base.export --target-runtime precompiled_qnn_onnx
   # or: --target-runtime qnn_context_binary
   ```
4. Requires free Qualcomm AI Hub account + API token (`qai-hub configure --api_token TOKEN`)
5. Alternatively, use Microsoft Olive CLI for the ONNX export/optimization

## Qualcomm AI Hub Models Repo
- https://github.com/qualcomm/ai-hub-models
- Whisper variants: tiny, base, small, small_quantized, medium, large_v3_turbo
- Also has **Zipformer** for true streaming ASR (~10ms/chunk on X Elite)

## Other Notable Local STT Models (2026)

| Model                | Params | WER    | Notes                            |
|----------------------|--------|--------|----------------------------------|
| Cohere Transcribe    | 2B     | ~5.42% | Current leaderboard top          |
| NVIDIA Canary Qwen   | 2.5B   | ~5.63% | Strong multilingual              |
| Qwen3-ASR            | various| SOTA   | Beats most commercial ASR        |
| NVIDIA Parakeet TDT  | 1.1B   | good   | Extremely fast (RTFx >2000)      |
| Whisper large-v3     | ~1.5B  | baseline | Most widely used                |

## Prompt Conditioning (Mode-Specific Vocabulary Biasing)

Whisper is not an LLM — it has no system prompt or chat. But it does support an
`initial_prompt` that conditions the decoder toward specific vocabulary. Use this
to improve accuracy per input mode.

**Terminal mode example:**
```
"ls, mkdir, cd, grep, git commit, git push, sudo apt install, chmod, ./configure, rm -rf, cat, echo, pip install, npm run"
```

**Code dictation mode example:**
```
"function, const, return, async, await, nullptr, struct, impl, void, int main, #include, import"
```

In whisper.cpp this maps to the `initial_prompt` field in `whisper_full_params`:
```c
params.initial_prompt = "ls, mkdir, cd, grep, git commit, sudo apt install, chmod";
```

In the ONNX/AI Hub pipeline, the `HfWhisperApp` uses HuggingFace's `WhisperProcessor`
which accepts `prompt_ids` — tokenize your prompt string and pass it as the decoder's
initial token sequence.

**If this isn't enough:** pipe Whisper output through a small local LLM (Phi/Qwen 2-3B
via Ollama) with a mode-specific system prompt to correct domain-specific misrecognitions
(e.g. "make dear" -> "mkdir"). Adds latency but replicates the OpenAI API behavior locally.

## App Integration Notes (C + SDL)

Pipeline for a floating-window STT utility:
1. Button press -> `SDL_OpenAudioDevice()` starts recording
2. Button release -> stop, convert PCM to mel spectrogram
3. Feed mel to ONNX Runtime whisper encoder -> decoder loop -> tokens
4. Decode tokens -> inject text into terminal (xdotool/wtype/clipboard)

### whisper.cpp fallback (simpler, CPU-only)

```c
#include "whisper.h"
struct whisper_context *ctx = whisper_init_from_file("ggml-base.en.bin");
struct whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
params.single_segment = true;
whisper_full(ctx, params, pcm_f32, n_samples);
const char *text = whisper_full_get_segment_text(ctx, 0);
```
