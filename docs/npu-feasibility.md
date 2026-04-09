# NPU Feasibility

## Bottom Line

Yes, a local NPU transcription path is realistic for this project on a Windows ARM64 Surface Pro 11 class device.

The viable route is:

1. Export a Whisper-family model to ONNX or a precompiled QNN artifact.
2. Run it through ONNX Runtime with the QNN Execution Provider on Windows ARM64.
3. Keep the current UI/audio/injection flow and swap only the transcription backend.

## Why This Looks Real

- ONNX Runtime officially documents the QNN Execution Provider for Qualcomm NPUs on Windows ARM64.
- Qualcomm AI Hub publishes Whisper-family model assets and export flows targeting QNN runtimes.
- Qualcomm also ships the AI Engine Direct helper/AppBuilder wrapper as a Windows ARM64 integration option.

Primary sources:

- https://onnxruntime.ai/docs/execution-providers/QNN-ExecutionProvider.html
- https://github.com/quic/ai-hub-models
- https://github.com/quic/ai-engine-direct-helper
- https://app.aihub.qualcomm.com/docs/hub/compile_examples.html

## What I Changed In This Repo

- Added a transcription backend seam so the UI no longer calls the OpenAI HTTP path directly.
- Kept the current OpenAI implementation working behind that seam.
- Added `.env` backend selection via `TB_TRANSCRIPTION_BACKEND=openai|npu`.
- Added a native NPU smoke-test path that uses ONNX Runtime + QNN to create Whisper encoder and decoder sessions and run a dummy inference pass against the exported Qualcomm package.

Current status:

- `openai` works in this build.
- `npu` probes the staged runtime and model package successfully.
- `transcription-probe --smoke` successfully creates QNN sessions and runs a dummy encoder + one-token decoder pass on the exported `whisper_base` package.
- Full end-to-end speech-to-text is still not implemented in the app backend.

## What Still Needs To Be Built

1. Build the Whisper audio frontend in native code.
2. Add tokenizer assets and detokenization.
3. Replace the placeholder NPU backend execution path with a real greedy decode loop.
4. Benchmark actual latency on your specific Surface hardware.

I would choose `ONNX Runtime + QNN EP` first because it fits the current C app better than introducing a custom runtime stack.

## Caveat

The latency numbers in `whispr-flow-npu-reference.md` are plausible but I did not validate them here. Treat them as directional until we run a real benchmark on your machine.

## Recommended Next Step

Build a narrow spike:

1. Keep the current app flow exactly as-is.
2. Add a second backend implementation that accepts captured PCM and returns final text only.
3. Start with offline, non-streaming `whisper_base.en`.
4. Add streaming or partial results only after the offline path is stable.

## MVP Decisions

These are the current locked-in scope decisions for the first real local STT implementation:

1. Offline only.
2. One utterance at a time.
3. English-only `whisper_base.en`.
4. Final text only, no timestamps.
5. Greedy decode only.
6. No streaming partials.
7. No timestamp alignment or subtitle-style metadata.
8. Keep the current UI flow and swap only the transcription backend internals.

Definitions:

- An utterance is one captured speech segment that starts when the user begins recording and ends when they stop.
- Timestamps are timing offsets attached to transcript segments or words.
- Greedy decode means choosing the highest-probability next token at each decoder step instead of exploring multiple candidates with beam search.

## Current Repo State

`TB_TRANSCRIPTION_BACKEND=npu` now does a real staging probe, and the repo also includes a native smoke test for the exported encoder and decoder models.

Expected default layout for a `precompiled_qnn_onnx` package:

```text
runtime/npu/onnxruntime.dll
runtime/npu/QnnHtp.dll
models/whisper_qnn/HfWhisperEncoder.onnx
models/whisper_qnn/HfWhisperEncoder_qairt_context.bin
models/whisper_qnn/HfWhisperDecoder.onnx
models/whisper_qnn/HfWhisperDecoder_qairt_context.bin
```

If you prefer a different layout, override it with:

- `TB_NPU_RUNTIME_DIR`
- `TB_NPU_PACKAGE_DIR`
- `TB_NPU_ORT_DLL`
- `TB_NPU_QNN_BACKEND_DLL`
- `TB_NPU_ENCODER_PATH`
- `TB_NPU_ENCODER_CONTEXT_BIN_PATH`
- `TB_NPU_DECODER_PATH`
- `TB_NPU_DECODER_CONTEXT_BIN_PATH`

The original `TB_NPU_MODEL_PATH` and `TB_NPU_CONTEXT_BIN_PATH` env vars are still accepted as backward-compatible aliases for the encoder model and encoder context.

You can now run a native smoke test with the staged runtime and exported model package:

```powershell
@'
TB_TRANSCRIPTION_BACKEND=npu
'@ | Set-Content build\probe-real-npu.env

.\build\Debug\transcription-probe.exe .\build\probe-real-npu.env --smoke
```

That smoke test creates ONNX Runtime QNN sessions for the exported encoder and decoder, runs a dummy encoder pass, feeds the resulting cross-attention caches into a one-token decoder pass, and checks that the logits tensor comes back with the expected shape. It is a runtime-validation step, not full speech-to-text.

The smoke run currently emits QNN warnings about `MapDmaData` / file mapping, then retries and succeeds. That is worth tracking during later performance and packaging work, but it does not block the current MVP path.
