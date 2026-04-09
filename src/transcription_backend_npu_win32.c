#include "transcription_backend.h"

#include <stdarg.h>

#if defined(_WIN32) && defined(TB_HAVE_NPU_STT)

#include <npu_stt.h>

#ifndef TERMINAL_BUDDY_NPU_STT_SOURCE_DIR
#define TERMINAL_BUDDY_NPU_STT_SOURCE_DIR ""
#endif

typedef struct TbNpuBackendState {
    SDL_Mutex *mutex;
    npu_stt_session *session;
    TbTranscriptionNpuTimingStats last_timing;
    char model[TB_TRANSCRIPTION_MODEL_MAX];
    char runtime_dir[TB_TRANSCRIPTION_PATH_MAX];
    char package_dir[TB_TRANSCRIPTION_PATH_MAX];
    char tokenizer_vocab_path[TB_TRANSCRIPTION_PATH_MAX];
    char mel_filters_path[TB_TRANSCRIPTION_PATH_MAX];
    Uint32 idle_cache_ms;
} TbNpuBackendState;

static TbNpuBackendState g_tb_npu_state;

static void tb_copy_text(char *dest, size_t dest_size, const char *value) {
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (value == NULL) {
        dest[0] = '\0';
        return;
    }

    SDL_snprintf(dest, dest_size, "%s", value);
}

static void tb_set_status(TbTranscriptionConfig *config, const char *status, const char *format, ...) {
    va_list args;

    SDL_snprintf(config->missing_status, sizeof(config->missing_status), "%s", status);
    va_start(args, format);
    SDL_vsnprintf(config->missing_detail, sizeof(config->missing_detail), format, args);
    va_end(args);
}

static bool tb_is_absolute_path(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    if ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/')) {
        return true;
    }

    return SDL_strlen(path) > 2 && path[1] == ':';
}

static void tb_join_path(char *dest, size_t dest_size, const char *left, const char *right) {
    size_t left_length = 0;

    if (right == NULL || right[0] == '\0') {
        tb_copy_text(dest, dest_size, left);
        return;
    }

    if (left == NULL || left[0] == '\0' || tb_is_absolute_path(right)) {
        tb_copy_text(dest, dest_size, right);
        return;
    }

    left_length = SDL_strlen(left);
    if (left_length > 0 && (left[left_length - 1] == '\\' || left[left_length - 1] == '/')) {
        SDL_snprintf(dest, dest_size, "%s%s", left, right);
    } else {
        SDL_snprintf(dest, dest_size, "%s\\%s", left, right);
    }
}

static const char *tb_default_package_subpath_for_model(const char *model) {
    if (model != NULL && SDL_strcmp(model, "whisper_tiny_en") == 0) {
        return "models\\whisper-tiny-en-qnn";
    }

    if (model != NULL && SDL_strcmp(model, "whisper_small_en") == 0) {
        return "models\\whisper-small-en-qnn";
    }

    if (model != NULL && SDL_strcmp(model, "whisper_large_v3_turbo") == 0) {
        return "build\\local-models\\whisper-large-v3-turbo-root\\whisper-large-v3-turbo-qnn";
    }

    return "models\\whisper-base-en-qnn";
}

static const char *tb_default_tokenizer_vocab_subpath_for_model(const char *model) {
    if (model != NULL && SDL_strcmp(model, "whisper_large_v3_turbo") == 0) {
        return "build\\local-models\\whisper-large-v3-turbo-root\\whisper-large-v3-assets\\vocab_by_id.txt";
    }

    return "models\\whisper-base-assets\\vocab_by_id.txt";
}

static const char *tb_default_mel_filters_subpath_for_model(const char *model) {
    if (model != NULL && SDL_strcmp(model, "whisper_large_v3_turbo") == 0) {
        return "build\\local-models\\whisper-large-v3-turbo-root\\whisper-large-v3-assets\\mel_filters_201x128_f32.bin";
    }

    return "models\\whisper-base-assets\\mel_filters_201x80_f32.bin";
}

static void tb_build_npu_stt_repo_default(
    char *dest,
    size_t dest_size,
    const char *relative_path
) {
    if (TERMINAL_BUDDY_NPU_STT_SOURCE_DIR[0] == '\0') {
        dest[0] = '\0';
        return;
    }

    tb_join_path(dest, dest_size, TERMINAL_BUDDY_NPU_STT_SOURCE_DIR, relative_path);
}

static bool tb_npu_ensure_mutex(void) {
    if (g_tb_npu_state.mutex != NULL) {
        return true;
    }

    g_tb_npu_state.mutex = SDL_CreateMutex();
    return g_tb_npu_state.mutex != NULL;
}

static void tb_npu_clear_last_timing_locked(void) {
    SDL_zero(g_tb_npu_state.last_timing);
}

static void tb_npu_copy_metrics(TbTranscriptionNpuTimingStats *dest, const npu_stt_metrics *src) {
    SDL_zero(*dest);
    if (src == NULL) {
        return;
    }

    dest->valid = src->valid;
    dest->asset_cache_hit = src->asset_cache_hit;
    dest->runtime_cache_hit = src->runtime_cache_hit;
    dest->chunk_count = src->chunk_count;
    dest->decoder_steps = src->decoder_steps;
    dest->emitted_token_count = src->emitted_token_count;
    dest->asset_load_ms = src->asset_load_ms;
    dest->resample_ms = src->resample_ms;
    dest->feature_ms = src->feature_ms;
    dest->token_decode_ms = src->token_decode_ms;
    dest->session_init_ms = src->session_init_ms;
    dest->encoder_ms = src->encoder_ms;
    dest->decoder_ms = src->decoder_ms;
    dest->decoder_setup_ms = src->decoder_setup_ms;
    dest->decoder_argmax_ms = src->decoder_argmax_ms;
    dest->decoder_bookkeeping_ms = src->decoder_bookkeeping_ms;
    dest->decoder_driver_ms = src->decoder_driver_ms;
    dest->total_ms = src->total_ms;
}

static double tb_npu_ticks_to_ms(Uint64 tick_delta) {
    Uint64 frequency = SDL_GetPerformanceFrequency();
    if (frequency == 0) {
        return 0.0;
    }

    return ((double) tick_delta * 1000.0) / (double) frequency;
}

static void tb_npu_clear_session_snapshot_locked(void) {
    g_tb_npu_state.model[0] = '\0';
    g_tb_npu_state.runtime_dir[0] = '\0';
    g_tb_npu_state.package_dir[0] = '\0';
    g_tb_npu_state.tokenizer_vocab_path[0] = '\0';
    g_tb_npu_state.mel_filters_path[0] = '\0';
    g_tb_npu_state.idle_cache_ms = 0;
}

static void tb_npu_destroy_session_locked(void) {
    if (g_tb_npu_state.session != NULL) {
        npu_stt_session_destroy(g_tb_npu_state.session);
        g_tb_npu_state.session = NULL;
    }

    tb_npu_clear_session_snapshot_locked();
    tb_npu_clear_last_timing_locked();
}

static bool tb_npu_config_matches_session_locked(const TbTranscriptionConfig *config) {
    if (config == NULL || g_tb_npu_state.session == NULL) {
        return false;
    }

    return SDL_strcmp(g_tb_npu_state.model, config->model) == 0
        && SDL_strcmp(g_tb_npu_state.runtime_dir, config->runtime_dir) == 0
        && SDL_strcmp(g_tb_npu_state.package_dir, config->package_dir) == 0
        && SDL_strcmp(g_tb_npu_state.tokenizer_vocab_path, config->tokenizer_vocab_path) == 0
        && SDL_strcmp(g_tb_npu_state.mel_filters_path, config->mel_filters_path) == 0
        && g_tb_npu_state.idle_cache_ms == config->npu_cache_idle_ms;
}

static void tb_npu_snapshot_config_locked(const TbTranscriptionConfig *config) {
    tb_copy_text(g_tb_npu_state.model, sizeof(g_tb_npu_state.model), config->model);
    tb_copy_text(g_tb_npu_state.runtime_dir, sizeof(g_tb_npu_state.runtime_dir), config->runtime_dir);
    tb_copy_text(g_tb_npu_state.package_dir, sizeof(g_tb_npu_state.package_dir), config->package_dir);
    tb_copy_text(
        g_tb_npu_state.tokenizer_vocab_path,
        sizeof(g_tb_npu_state.tokenizer_vocab_path),
        config->tokenizer_vocab_path
    );
    tb_copy_text(
        g_tb_npu_state.mel_filters_path,
        sizeof(g_tb_npu_state.mel_filters_path),
        config->mel_filters_path
    );
    g_tb_npu_state.idle_cache_ms = config->npu_cache_idle_ms;
}

static bool tb_npu_create_session_locked(const TbTranscriptionConfig *config, char **out_error) {
    npu_stt_session_config session_config;
    npu_stt_status status;
    char *internal_error = NULL;
    npu_stt_session *session = NULL;

    if (out_error != NULL) {
        *out_error = NULL;
    }

    if (config == NULL) {
        if (out_error != NULL) {
            *out_error = SDL_strdup("Invalid NPU configuration.");
        }
        return false;
    }

    if (tb_npu_config_matches_session_locked(config)) {
        return true;
    }

    tb_npu_destroy_session_locked();

    npu_stt_session_config_init(&session_config);
    session_config.model_id = config->model[0] != '\0' ? config->model : NULL;
    session_config.enable_warm_cache = config->npu_cache_idle_ms > 0;
    session_config.idle_cache_ms = config->npu_cache_idle_ms > 0 ? config->npu_cache_idle_ms : NPU_STT_DEFAULT_IDLE_CACHE_MS;
    session_config.artifacts.runtime_dir = config->runtime_dir;
    session_config.artifacts.package_dir = config->package_dir;
    session_config.artifacts.tokenizer_vocab_path = config->tokenizer_vocab_path[0] != '\0' ? config->tokenizer_vocab_path : NULL;
    session_config.artifacts.mel_filters_path = config->mel_filters_path[0] != '\0' ? config->mel_filters_path : NULL;

    status = npu_stt_session_create(&session_config, &session, &internal_error);
    if (status != NPU_STT_STATUS_OK || session == NULL) {
        if (out_error != NULL) {
            if (internal_error != NULL) {
                *out_error = SDL_strdup(internal_error);
            } else {
                SDL_asprintf(
                    out_error,
                    "npu_stt_session_create failed: %s",
                    npu_stt_status_string(status)
                );
            }
        }
        npu_stt_free_string(internal_error);
        if (session != NULL) {
            npu_stt_session_destroy(session);
        }
        return false;
    }

    npu_stt_free_string(internal_error);
    g_tb_npu_state.session = session;
    tb_npu_snapshot_config_locked(config);
    return true;
}

static bool tb_npu_prepare_audio(
    const TbTranscriptionRequest *request,
    npu_stt_audio_buffer_f32 *out_audio,
    float **out_owned_samples,
    char **out_error
) {
    SDL_AudioSpec src_spec;
    SDL_AudioSpec dst_spec;
    Uint8 *converted_data = NULL;
    int converted_length = 0;

    if (out_audio != NULL) {
        SDL_zero(*out_audio);
    }
    if (out_owned_samples != NULL) {
        *out_owned_samples = NULL;
    }
    if (out_error != NULL) {
        *out_error = NULL;
    }

    if (request == NULL || out_audio == NULL || out_owned_samples == NULL) {
        if (out_error != NULL) {
            *out_error = SDL_strdup("Invalid NPU audio conversion request.");
        }
        return false;
    }

    if (
        request->samples == NULL
        || request->sample_count <= 0
        || request->sample_rate <= 0
    ) {
        if (out_error != NULL) {
            *out_error = SDL_strdup("Transcription request is missing audio samples.");
        }
        return false;
    }

    if (request->sample_rate == NPU_STT_REQUIRED_SAMPLE_RATE) {
        out_audio->samples = request->samples;
        out_audio->sample_count = request->sample_count;
        out_audio->sample_rate = request->sample_rate;
        return true;
    }

    SDL_zero(src_spec);
    SDL_zero(dst_spec);
    src_spec.format = SDL_AUDIO_F32;
    src_spec.channels = 1;
    src_spec.freq = request->sample_rate;
    dst_spec.format = SDL_AUDIO_F32;
    dst_spec.channels = 1;
    dst_spec.freq = NPU_STT_REQUIRED_SAMPLE_RATE;

    if (!SDL_ConvertAudioSamples(
            &src_spec,
            (const Uint8 *) request->samples,
            request->sample_count * (int) sizeof(float),
            &dst_spec,
            &converted_data,
            &converted_length
        )) {
        if (out_error != NULL) {
            SDL_asprintf(
                out_error,
                "SDL_ConvertAudioSamples failed while preparing %d Hz input for npu-stt-c: %s",
                request->sample_rate,
                SDL_GetError()
            );
        }
        return false;
    }

    if (converted_length <= 0 || (converted_length % (int) sizeof(float)) != 0) {
        SDL_free(converted_data);
        if (out_error != NULL) {
            *out_error = SDL_strdup("Audio conversion produced an invalid sample buffer.");
        }
        return false;
    }

    *out_owned_samples = (float *) converted_data;
    out_audio->samples = (const float *) converted_data;
    out_audio->sample_count = converted_length / (int) sizeof(float);
    out_audio->sample_rate = dst_spec.freq;
    return true;
}

void tb_transcription_backend_npu_configure(
    TbTranscriptionConfig *config,
    const char *runtime_dir_override,
    const char *package_dir_override,
    const char *tokenizer_vocab_override,
    const char *mel_filters_override
) {
    if (config == NULL) {
        return;
    }

    if (runtime_dir_override != NULL && runtime_dir_override[0] != '\0') {
        tb_copy_text(config->runtime_dir, sizeof(config->runtime_dir), runtime_dir_override);
    } else {
        tb_build_npu_stt_repo_default(
            config->runtime_dir,
            sizeof(config->runtime_dir),
            "runtime\\windows-arm64"
        );
    }

    if (package_dir_override != NULL && package_dir_override[0] != '\0') {
        tb_copy_text(config->package_dir, sizeof(config->package_dir), package_dir_override);
    } else {
        tb_build_npu_stt_repo_default(
            config->package_dir,
            sizeof(config->package_dir),
            tb_default_package_subpath_for_model(config->model)
        );
    }

    if (tokenizer_vocab_override != NULL && tokenizer_vocab_override[0] != '\0') {
        tb_copy_text(config->tokenizer_vocab_path, sizeof(config->tokenizer_vocab_path), tokenizer_vocab_override);
    } else {
        tb_build_npu_stt_repo_default(
            config->tokenizer_vocab_path,
            sizeof(config->tokenizer_vocab_path),
            tb_default_tokenizer_vocab_subpath_for_model(config->model)
        );
    }

    if (mel_filters_override != NULL && mel_filters_override[0] != '\0') {
        tb_copy_text(config->mel_filters_path, sizeof(config->mel_filters_path), mel_filters_override);
    } else {
        tb_build_npu_stt_repo_default(
            config->mel_filters_path,
            sizeof(config->mel_filters_path),
            tb_default_mel_filters_subpath_for_model(config->model)
        );
    }
}

void tb_transcription_backend_npu_probe(TbTranscriptionConfig *config) {
    char *error_text = NULL;

    if (config == NULL) {
        return;
    }

    config->ready = false;
    if (!tb_npu_ensure_mutex()) {
        tb_set_status(config, "NPU BACKEND ERROR", "%s", "Failed to create the NPU backend mutex.");
        return;
    }

    SDL_LockMutex(g_tb_npu_state.mutex);
    tb_npu_clear_last_timing_locked();
    if (!tb_npu_create_session_locked(config, &error_text)) {
        tb_set_status(
            config,
            "NPU BACKEND NOT READY",
            "%s",
            error_text != NULL ? error_text : "npu-stt-c could not validate the local NPU backend."
        );
        SDL_UnlockMutex(g_tb_npu_state.mutex);
        SDL_free(error_text);
        return;
    }
    SDL_UnlockMutex(g_tb_npu_state.mutex);

    config->ready = true;
    tb_set_status(
        config,
        "NPU BACKEND READY",
        "npu-stt-c validated %s using runtime %s and package %s.",
        config->model,
        config->runtime_dir,
        config->package_dir
    );
    SDL_free(error_text);
}

bool tb_transcription_backend_npu_execute(
    const TbTranscriptionConfig *config,
    const TbTranscriptionRequest *request,
    char **out_text,
    char **out_error
) {
    npu_stt_audio_buffer_f32 audio;
    npu_stt_result result;
    npu_stt_status status;
    char *internal_error = NULL;
    float *owned_samples = NULL;
    double wrapper_resample_ms = 0.0;
    bool success = false;

    if (out_text != NULL) {
        *out_text = NULL;
    }
    if (out_error != NULL) {
        *out_error = NULL;
    }

    if (!tb_npu_ensure_mutex()) {
        if (out_error != NULL) {
            *out_error = SDL_strdup("Failed to create the NPU backend mutex.");
        }
        return false;
    }

    SDL_zero(audio);
    npu_stt_result_init(&result);

    SDL_LockMutex(g_tb_npu_state.mutex);

    if (!tb_npu_create_session_locked(config, &internal_error)) {
        goto cleanup;
    }

    {
        Uint64 stage_start = SDL_GetPerformanceCounter();

        if (!tb_npu_prepare_audio(request, &audio, &owned_samples, &internal_error)) {
            goto cleanup;
        }
        wrapper_resample_ms = tb_npu_ticks_to_ms(SDL_GetPerformanceCounter() - stage_start);
    }

    status = npu_stt_session_transcribe_f32(g_tb_npu_state.session, &audio, &result, &internal_error);
    if (status != NPU_STT_STATUS_OK) {
        if (internal_error == NULL) {
            SDL_asprintf(
                &internal_error,
                "npu_stt_session_transcribe_f32 failed: %s",
                npu_stt_status_string(status)
            );
        }
        tb_npu_clear_last_timing_locked();
        goto cleanup;
    }

    if (out_text != NULL) {
        *out_text = SDL_strdup(result.text != NULL ? result.text : "");
        if (*out_text == NULL) {
            internal_error = SDL_strdup("Failed to copy transcription text.");
            tb_npu_clear_last_timing_locked();
            goto cleanup;
        }
    }

    tb_npu_copy_metrics(&g_tb_npu_state.last_timing, &result.metrics);
    g_tb_npu_state.last_timing.resample_ms += wrapper_resample_ms;
    g_tb_npu_state.last_timing.total_ms += wrapper_resample_ms;
    success = true;

cleanup:
    npu_stt_result_destroy(&result);
    SDL_free(owned_samples);

    if (!success && out_error != NULL) {
        *out_error = internal_error != NULL ? internal_error : SDL_strdup("Local NPU transcription failed.");
        internal_error = NULL;
    }

    SDL_UnlockMutex(g_tb_npu_state.mutex);
    SDL_free(internal_error);
    return success;
}

bool tb_transcription_backend_npu_smoke_test(
    const TbTranscriptionConfig *config,
    char **out_detail
) {
    char *error_text = NULL;

    if (out_detail != NULL) {
        *out_detail = NULL;
    }

    if (!tb_npu_ensure_mutex()) {
        if (out_detail != NULL) {
            *out_detail = SDL_strdup("Failed to create the NPU backend mutex.");
        }
        return false;
    }

    SDL_LockMutex(g_tb_npu_state.mutex);
    if (!tb_npu_create_session_locked(config, &error_text)) {
        SDL_UnlockMutex(g_tb_npu_state.mutex);
        if (out_detail != NULL) {
            *out_detail = error_text != NULL ? error_text : SDL_strdup("npu-stt-c session validation failed.");
        } else {
            SDL_free(error_text);
        }
        return false;
    }
    SDL_UnlockMutex(g_tb_npu_state.mutex);

    if (out_detail != NULL) {
        SDL_asprintf(
            out_detail,
            "npu-stt-c validated session creation for %s using runtime %s and package %s.",
            config != NULL && config->model[0] != '\0' ? config->model : "whisper_base_en",
            config != NULL ? config->runtime_dir : "",
            config != NULL ? config->package_dir : ""
        );
    }
    SDL_free(error_text);
    return true;
}

bool tb_transcription_backend_npu_get_last_timing_stats(
    TbTranscriptionNpuTimingStats *out_stats
) {
    bool valid = false;

    if (out_stats == NULL || !tb_npu_ensure_mutex()) {
        return false;
    }

    SDL_LockMutex(g_tb_npu_state.mutex);
    valid = g_tb_npu_state.last_timing.valid;
    if (valid) {
        *out_stats = g_tb_npu_state.last_timing;
    }
    SDL_UnlockMutex(g_tb_npu_state.mutex);
    return valid;
}

void tb_transcription_backend_npu_pump(Uint64 now_ms, Uint32 idle_timeout_ms) {
    (void) idle_timeout_ms;

    if (!tb_npu_ensure_mutex()) {
        return;
    }

    SDL_LockMutex(g_tb_npu_state.mutex);
    if (g_tb_npu_state.session != NULL) {
        npu_stt_session_pump(g_tb_npu_state.session, (uint64_t) now_ms);
    }
    SDL_UnlockMutex(g_tb_npu_state.mutex);
}

void tb_transcription_backend_npu_shutdown(void) {
    SDL_Mutex *mutex = NULL;

    if (g_tb_npu_state.mutex == NULL) {
        return;
    }

    mutex = g_tb_npu_state.mutex;
    SDL_LockMutex(mutex);
    tb_npu_destroy_session_locked();
    SDL_UnlockMutex(mutex);
    SDL_DestroyMutex(mutex);
    g_tb_npu_state.mutex = NULL;
}

#else

void tb_transcription_backend_npu_configure(
    TbTranscriptionConfig *config,
    const char *runtime_dir_override,
    const char *package_dir_override,
    const char *tokenizer_vocab_override,
    const char *mel_filters_override
) {
    (void) config;
    (void) runtime_dir_override;
    (void) package_dir_override;
    (void) tokenizer_vocab_override;
    (void) mel_filters_override;
}

void tb_transcription_backend_npu_probe(TbTranscriptionConfig *config) {
    if (config == NULL) {
        return;
    }

    config->ready = false;
    SDL_snprintf(config->missing_status, sizeof(config->missing_status), "%s", "NPU BACKEND NOT BUILT");
    SDL_snprintf(
        config->missing_detail,
        sizeof(config->missing_detail),
        "%s",
        "terminal-buddy was built without npu-stt-c. Set TERMINAL_BUDDY_NPU_STT_SOURCE_DIR to a local npu-stt-c checkout and rebuild."
    );
}

bool tb_transcription_backend_npu_execute(
    const TbTranscriptionConfig *config,
    const TbTranscriptionRequest *request,
    char **out_text,
    char **out_error
) {
    (void) config;
    (void) request;
    (void) out_text;

    if (out_error != NULL) {
        *out_error = SDL_strdup(
            "The NPU backend is unavailable because terminal-buddy was built without npu-stt-c."
        );
    }
    return false;
}

bool tb_transcription_backend_npu_smoke_test(
    const TbTranscriptionConfig *config,
    char **out_detail
) {
    (void) config;

    if (out_detail != NULL) {
        *out_detail = SDL_strdup(
            "The NPU backend is unavailable because terminal-buddy was built without npu-stt-c."
        );
    }
    return false;
}

bool tb_transcription_backend_npu_get_last_timing_stats(
    TbTranscriptionNpuTimingStats *out_stats
) {
    (void) out_stats;
    return false;
}

void tb_transcription_backend_npu_pump(Uint64 now_ms, Uint32 idle_timeout_ms) {
    (void) now_ms;
    (void) idle_timeout_ms;
}

void tb_transcription_backend_npu_shutdown(void) {
}

#endif
