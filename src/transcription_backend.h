#ifndef TRANSCRIPTION_BACKEND_H
#define TRANSCRIPTION_BACKEND_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#define TB_TRANSCRIPTION_BACKEND_NAME_MAX 32
#define TB_TRANSCRIPTION_MODEL_MAX 64
#define TB_TRANSCRIPTION_API_KEY_MAX 512
#define TB_TRANSCRIPTION_STATUS_MAX 256
#define TB_TRANSCRIPTION_DETAIL_MAX 512
#define TB_TRANSCRIPTION_PATH_MAX 512
#define TB_NPU_CACHE_IDLE_DEFAULT_MS 60000u

typedef enum TbTranscriptionBackendKind {
    TB_TRANSCRIPTION_BACKEND_OPENAI = 0,
    TB_TRANSCRIPTION_BACKEND_NPU
} TbTranscriptionBackendKind;

typedef struct TbTranscriptionConfig {
    TbTranscriptionBackendKind backend;
    bool ready;
    char backend_name[TB_TRANSCRIPTION_BACKEND_NAME_MAX];
    char model[TB_TRANSCRIPTION_MODEL_MAX];
    char npu_model[TB_TRANSCRIPTION_MODEL_MAX];
    char api_key[TB_TRANSCRIPTION_API_KEY_MAX];
    char install_root[TB_TRANSCRIPTION_PATH_MAX];
    char runtime_dir[TB_TRANSCRIPTION_PATH_MAX];
    char package_dir[TB_TRANSCRIPTION_PATH_MAX];
    char tokenizer_vocab_path[TB_TRANSCRIPTION_PATH_MAX];
    char mel_filters_path[TB_TRANSCRIPTION_PATH_MAX];
    Uint32 npu_cache_idle_ms;
    char missing_status[TB_TRANSCRIPTION_STATUS_MAX];
    char missing_detail[TB_TRANSCRIPTION_DETAIL_MAX];
} TbTranscriptionConfig;

typedef struct TbTranscriptionRequest {
    const float *samples;
    int sample_count;
    int sample_rate;
    const char *prompt;
} TbTranscriptionRequest;

typedef struct TbTranscriptionNpuTimingStats {
    bool valid;
    bool asset_cache_hit;
    bool runtime_cache_hit;
    int chunk_count;
    int decoder_steps;
    int emitted_token_count;
    double asset_load_ms;
    double resample_ms;
    double feature_ms;
    double token_decode_ms;
    double session_init_ms;
    double encoder_ms;
    double decoder_ms;
    double decoder_setup_ms;
    double decoder_argmax_ms;
    double decoder_bookkeeping_ms;
    double decoder_driver_ms;
    double total_ms;
} TbTranscriptionNpuTimingStats;

void tb_transcription_config_init(TbTranscriptionConfig *config);
void tb_transcription_reload_env(TbTranscriptionConfig *config, const char *env_path);
bool tb_transcription_execute(
    const TbTranscriptionConfig *config,
    const TbTranscriptionRequest *request,
    char **out_text,
    char **out_error
);

bool tb_transcription_backend_openai_execute(
    const TbTranscriptionConfig *config,
    const TbTranscriptionRequest *request,
    char **out_text,
    char **out_error
);

void tb_transcription_backend_npu_configure(
    TbTranscriptionConfig *config,
    const char *install_root_override,
    const char *runtime_dir_override,
    const char *package_dir_override,
    const char *tokenizer_vocab_override,
    const char *mel_filters_override
);

void tb_transcription_backend_npu_probe(TbTranscriptionConfig *config);

bool tb_transcription_backend_npu_execute(
    const TbTranscriptionConfig *config,
    const TbTranscriptionRequest *request,
    char **out_text,
    char **out_error
);

bool tb_transcription_backend_npu_smoke_test(
    const TbTranscriptionConfig *config,
    char **out_detail
);

bool tb_transcription_backend_npu_get_last_timing_stats(
    TbTranscriptionNpuTimingStats *out_stats
);

void tb_transcription_backend_npu_pump(Uint64 now_ms, Uint32 idle_timeout_ms);

void tb_transcription_backend_npu_shutdown(void);

#endif
