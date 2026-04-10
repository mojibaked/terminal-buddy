#include <stdio.h>

#include <SDL3/SDL.h>

#include "transcription_backend.h"

#ifndef TERMINAL_BUDDY_SOURCE_DIR
#define TERMINAL_BUDDY_SOURCE_DIR "."
#endif

static char *build_default_env_path(void) {
    char *path = NULL;
    if (SDL_asprintf(&path, "%s/.env/dev.env", TERMINAL_BUDDY_SOURCE_DIR) < 0) {
        return NULL;
    }
    return path;
}

static bool load_probe_audio(
    const char *audio_path,
    float **out_samples,
    int *out_sample_count,
    int *out_sample_rate,
    char **out_error
) {
    SDL_AudioSpec src_spec;
    SDL_AudioSpec dst_spec;
    Uint8 *src_data = NULL;
    Uint8 *dst_data = NULL;
    Uint32 src_length = 0;
    int dst_length = 0;
    bool success = false;

    if (out_samples != NULL) {
        *out_samples = NULL;
    }
    if (out_sample_count != NULL) {
        *out_sample_count = 0;
    }
    if (out_sample_rate != NULL) {
        *out_sample_rate = 0;
    }
    if (out_error != NULL) {
        *out_error = NULL;
    }

    if (
        audio_path == NULL
        || audio_path[0] == '\0'
        || out_samples == NULL
        || out_sample_count == NULL
        || out_sample_rate == NULL
    ) {
        if (out_error != NULL) {
            *out_error = SDL_strdup("Probe audio path is missing.");
        }
        return false;
    }

    if (!SDL_LoadWAV(audio_path, &src_spec, &src_data, &src_length)) {
        if (out_error != NULL) {
            SDL_asprintf(out_error, "SDL_LoadWAV failed for %s: %s", audio_path, SDL_GetError());
        }
        goto cleanup;
    }

    SDL_zero(dst_spec);
    dst_spec.format = SDL_AUDIO_F32;
    dst_spec.channels = 1;
    dst_spec.freq = src_spec.freq;

    if (!SDL_ConvertAudioSamples(&src_spec, src_data, (int) src_length, &dst_spec, &dst_data, &dst_length)) {
        if (out_error != NULL) {
            SDL_asprintf(out_error, "SDL_ConvertAudioSamples failed for %s: %s", audio_path, SDL_GetError());
        }
        goto cleanup;
    }

    if (dst_length <= 0 || (dst_length % (int) sizeof(float)) != 0) {
        if (out_error != NULL) {
            SDL_asprintf(out_error, "Probe audio conversion produced an invalid buffer length for %s.", audio_path);
        }
        goto cleanup;
    }

    *out_samples = (float *) dst_data;
    *out_sample_count = dst_length / (int) sizeof(float);
    *out_sample_rate = dst_spec.freq;
    dst_data = NULL;
    success = true;

cleanup:
    SDL_free(dst_data);
    SDL_free(src_data);
    return success;
}

int main(int argc, char **argv) {
    TbTranscriptionConfig config;
    TbTranscriptionNpuTimingStats timing_stats;
    TbTranscriptionRequest request;
    char *default_env_path = NULL;
    const char *env_path = NULL;
    const char *audio_path = NULL;
    const char *prompt = NULL;
    bool run_smoke = false;
    int repeat_count = 1;
    float *audio_samples = NULL;
    int audio_sample_count = 0;
    int audio_sample_rate = 0;
    char *audio_error = NULL;
    char *response_text = NULL;
    char *error_text = NULL;
    int exit_code = 0;

    tb_transcription_config_init(&config);
    SDL_zero(timing_stats);

    for (int index = 1; index < argc; ++index) {
        if (argv[index] == NULL || argv[index][0] == '\0') {
            continue;
        }

        if (SDL_strcmp(argv[index], "--smoke") == 0) {
            run_smoke = true;
        } else if (SDL_strcmp(argv[index], "--audio") == 0 && (index + 1) < argc) {
            audio_path = argv[++index];
        } else if (SDL_strcmp(argv[index], "--prompt") == 0 && (index + 1) < argc) {
            prompt = argv[++index];
        } else if (SDL_strcmp(argv[index], "--repeat") == 0 && (index + 1) < argc) {
            const char *repeat_arg = argv[++index];
            repeat_count = SDL_max(1, repeat_arg != NULL ? SDL_atoi(repeat_arg) : 1);
        } else if (env_path == NULL) {
            env_path = argv[index];
        }
    }

    if (env_path == NULL) {
        default_env_path = build_default_env_path();
        env_path = default_env_path;
    }

    tb_transcription_reload_env(&config, env_path);

    printf("env_path=%s\n", env_path != NULL ? env_path : "");
    printf("backend=%s\n", config.backend_name);
    printf("ready=%d\n", config.ready ? 1 : 0);
    printf("model=%s\n", config.model);
    printf("status=%s\n", config.missing_status);
    printf("detail=%s\n", config.missing_detail);
    printf("install_root=%s\n", config.install_root);
    printf("runtime_dir=%s\n", config.runtime_dir);
    printf("package_dir=%s\n", config.package_dir);
    printf("tokenizer_vocab=%s\n", config.tokenizer_vocab_path);
    printf("mel_filters=%s\n", config.mel_filters_path);
    if (run_smoke) {
        char *smoke_detail = NULL;
        bool smoke_ok = tb_transcription_backend_npu_smoke_test(&config, &smoke_detail);
        printf("smoke=%d\n", smoke_ok ? 1 : 0);
        printf("smoke_detail=%s\n", smoke_detail != NULL ? smoke_detail : "");
        SDL_free(smoke_detail);
    }

    if (audio_path != NULL) {
        SDL_zero(request);

        if (!SDL_Init(SDL_INIT_AUDIO)) {
            printf("audio_path=%s\n", audio_path);
            printf("audio_error=SDL_Init failed: %s\n", SDL_GetError());
            exit_code = 1;
            goto cleanup;
        }

        if (!load_probe_audio(audio_path, &audio_samples, &audio_sample_count, &audio_sample_rate, &audio_error)) {
            printf("audio_path=%s\n", audio_path);
            printf("audio_error=%s\n", audio_error != NULL ? audio_error : "");
            exit_code = 1;
            goto cleanup;
        }

        request.samples = audio_samples;
        request.sample_count = audio_sample_count;
        request.sample_rate = audio_sample_rate;
        request.prompt = prompt;

        printf("audio_path=%s\n", audio_path);
        printf("audio_sample_rate=%d\n", audio_sample_rate);
        printf("audio_sample_count=%d\n", audio_sample_count);

        printf("repeat_count=%d\n", repeat_count);

        for (int run_index = 0; run_index < repeat_count; ++run_index) {
            SDL_free(response_text);
            response_text = NULL;
            SDL_free(error_text);
            error_text = NULL;
            SDL_zero(timing_stats);

            if (!tb_transcription_execute(&config, &request, &response_text, &error_text)) {
                printf("run_index=%d\n", run_index + 1);
                printf("transcribe=0\n");
                printf("transcribe_error=%s\n", error_text != NULL ? error_text : "");
                exit_code = 1;
                goto cleanup;
            }

            printf("run_index=%d\n", run_index + 1);
            printf("transcribe=1\n");
            printf("transcript=%s\n", response_text != NULL ? response_text : "");

            if (tb_transcription_backend_npu_get_last_timing_stats(&timing_stats)) {
                double decoder_ms_per_step = timing_stats.decoder_steps > 0
                    ? (timing_stats.decoder_ms / (double) timing_stats.decoder_steps)
                    : 0.0;
                double decoder_driver_ms_per_step = timing_stats.decoder_steps > 0
                    ? (timing_stats.decoder_driver_ms / (double) timing_stats.decoder_steps)
                    : 0.0;

                printf("timing_chunk_count=%d\n", timing_stats.chunk_count);
                printf("timing_decoder_steps=%d\n", timing_stats.decoder_steps);
                printf("timing_emitted_tokens=%d\n", timing_stats.emitted_token_count);
                printf("timing_asset_cache_hit=%d\n", timing_stats.asset_cache_hit ? 1 : 0);
                printf("timing_runtime_cache_hit=%d\n", timing_stats.runtime_cache_hit ? 1 : 0);
                printf("timing_asset_load_ms=%.3f\n", timing_stats.asset_load_ms);
                printf("timing_resample_ms=%.3f\n", timing_stats.resample_ms);
                printf("timing_feature_ms=%.3f\n", timing_stats.feature_ms);
                printf("timing_session_init_ms=%.3f\n", timing_stats.session_init_ms);
                printf("timing_encoder_ms=%.3f\n", timing_stats.encoder_ms);
                printf("timing_decoder_ms=%.3f\n", timing_stats.decoder_ms);
                printf("timing_decoder_ms_per_step=%.3f\n", decoder_ms_per_step);
                printf("timing_decoder_setup_ms=%.3f\n", timing_stats.decoder_setup_ms);
                printf("timing_decoder_argmax_ms=%.3f\n", timing_stats.decoder_argmax_ms);
                printf("timing_decoder_bookkeeping_ms=%.3f\n", timing_stats.decoder_bookkeeping_ms);
                printf("timing_decoder_driver_ms=%.3f\n", timing_stats.decoder_driver_ms);
                printf("timing_decoder_driver_ms_per_step=%.3f\n", decoder_driver_ms_per_step);
                printf("timing_token_decode_ms=%.3f\n", timing_stats.token_decode_ms);
                printf("timing_total_ms=%.3f\n", timing_stats.total_ms);
            }
        }
    }

cleanup:
    tb_transcription_backend_npu_shutdown();
    SDL_Quit();
    SDL_free(response_text);
    SDL_free(error_text);
    SDL_free(audio_error);
    SDL_free(audio_samples);
    SDL_free(default_env_path);
    return exit_code;
}
