#include "transcription_backend.h"

#include <stdlib.h>
#include <string.h>

bool tb_transcription_backend_openai_execute(
    const TbTranscriptionConfig *config,
    const TbTranscriptionRequest *request,
    char **out_text,
    char **out_error
);

static void parse_env_assignment(char *line, char **key, char **value) {
    char *separator = SDL_strchr(line, '=');
    if (separator == NULL) {
        *key = NULL;
        *value = NULL;
        return;
    }

    *separator = '\0';
    *key = line;
    *value = separator + 1;

    while (**key == ' ' || **key == '\t') {
        ++(*key);
    }
    while (**value == ' ' || **value == '\t') {
        ++(*value);
    }

    if (**value == '"' || **value == '\'') {
        char quote = **value;
        char *end = SDL_strrchr(*value + 1, quote);
        if (end != NULL) {
            *end = '\0';
        }
        ++(*value);
    }
}

static void trim_newlines(char *text) {
    size_t length = SDL_strlen(text);
    while (length > 0 && (text[length - 1] == '\r' || text[length - 1] == '\n' || text[length - 1] == ' ' || text[length - 1] == '\t')) {
        text[length - 1] = '\0';
        --length;
    }
}

static TbTranscriptionBackendKind parse_backend_kind(const char *value) {
    if (value == NULL || value[0] == '\0') {
        return TB_TRANSCRIPTION_BACKEND_OPENAI;
    }

    if (SDL_strcasecmp(value, "npu") == 0 || SDL_strcasecmp(value, "local") == 0 || SDL_strcasecmp(value, "local_npu") == 0) {
        return TB_TRANSCRIPTION_BACKEND_NPU;
    }

    return TB_TRANSCRIPTION_BACKEND_OPENAI;
}

static void apply_backend_defaults(TbTranscriptionConfig *config) {
    config->ready = false;

    switch (config->backend) {
        case TB_TRANSCRIPTION_BACKEND_NPU:
            SDL_snprintf(config->backend_name, sizeof(config->backend_name), "%s", "NPU");
            if (config->model[0] == '\0' || SDL_strcmp(config->model, "gpt-4o-transcribe") == 0) {
                SDL_snprintf(
                    config->model,
                    sizeof(config->model),
                    "%s",
                    config->npu_model[0] != '\0' ? config->npu_model : "whisper_base_en"
                );
            }
            SDL_snprintf(config->missing_status, sizeof(config->missing_status), "%s", "NPU BACKEND NOT BUILT");
            SDL_snprintf(
                config->missing_detail,
                sizeof(config->missing_detail),
                "%s",
                "Set TB_TRANSCRIPTION_BACKEND=npu and stage the sibling npu-stt-c runtime/model assets to enable local offline Whisper transcription."
            );
            break;
        case TB_TRANSCRIPTION_BACKEND_OPENAI:
        default:
            config->backend = TB_TRANSCRIPTION_BACKEND_OPENAI;
            SDL_snprintf(config->backend_name, sizeof(config->backend_name), "%s", "OpenAI");
            if (config->model[0] == '\0') {
                SDL_snprintf(config->model, sizeof(config->model), "%s", "gpt-4o-transcribe");
            }
            SDL_snprintf(config->missing_status, sizeof(config->missing_status), "%s", "OPENAI KEY MISSING");
            SDL_snprintf(
                config->missing_detail,
                sizeof(config->missing_detail),
                "%s",
                "Create .env/dev.env and set OPENAI_API_KEY=... then record again."
            );
            config->ready = config->api_key[0] != '\0';
            break;
    }
}

void tb_transcription_config_init(TbTranscriptionConfig *config) {
    SDL_zero(*config);
    config->backend = TB_TRANSCRIPTION_BACKEND_OPENAI;
    config->npu_cache_idle_ms = TB_NPU_CACHE_IDLE_DEFAULT_MS;
    SDL_snprintf(config->npu_model, sizeof(config->npu_model), "%s", "whisper_base_en");
    apply_backend_defaults(config);
}

void tb_transcription_reload_env(TbTranscriptionConfig *config, const char *env_path) {
    size_t size = 0;
    void *raw = NULL;

    tb_transcription_config_init(config);

    if (env_path == NULL) {
        return;
    }

    raw = SDL_LoadFile(env_path, &size);
    if (raw == NULL) {
        return;
    }

    char *contents = (char *) SDL_malloc(size + 1);
    if (contents != NULL) {
        char *context = NULL;
        char *line = NULL;
        char backend_value[TB_TRANSCRIPTION_BACKEND_NAME_MAX];
        char openai_model_value[TB_TRANSCRIPTION_MODEL_MAX];
        char npu_model_value[TB_TRANSCRIPTION_MODEL_MAX];
        char runtime_dir_value[TB_TRANSCRIPTION_PATH_MAX];
        char package_dir_value[TB_TRANSCRIPTION_PATH_MAX];
        char tokenizer_vocab_value[TB_TRANSCRIPTION_PATH_MAX];
        char mel_filters_value[TB_TRANSCRIPTION_PATH_MAX];
        char npu_cache_idle_ms_value[32];

        SDL_memcpy(contents, raw, size);
        contents[size] = '\0';
        backend_value[0] = '\0';
        openai_model_value[0] = '\0';
        npu_model_value[0] = '\0';
        runtime_dir_value[0] = '\0';
        package_dir_value[0] = '\0';
        tokenizer_vocab_value[0] = '\0';
        mel_filters_value[0] = '\0';
        npu_cache_idle_ms_value[0] = '\0';

        line = strtok_s(contents, "\r\n", &context);
        while (line != NULL) {
            char *key = NULL;
            char *value = NULL;

            if (line[0] != '#' && line[0] != '\0') {
                parse_env_assignment(line, &key, &value);
                if (key != NULL && value != NULL) {
                    if (SDL_strcmp(key, "TB_TRANSCRIPTION_BACKEND") == 0 || SDL_strcmp(key, "TRANSCRIPTION_BACKEND") == 0) {
                        SDL_snprintf(backend_value, sizeof(backend_value), "%s", value);
                    } else if (SDL_strcmp(key, "OPENAI_API_KEY") == 0) {
                        SDL_snprintf(config->api_key, sizeof(config->api_key), "%s", value);
                    } else if (SDL_strcmp(key, "OPENAI_TRANSCRIBE_MODEL") == 0) {
                        SDL_snprintf(openai_model_value, sizeof(openai_model_value), "%s", value);
                    } else if (SDL_strcmp(key, "TB_TRANSCRIPTION_MODEL") == 0) {
                        SDL_snprintf(npu_model_value, sizeof(npu_model_value), "%s", value);
                    } else if (SDL_strcmp(key, "TB_NPU_RUNTIME_DIR") == 0) {
                        SDL_snprintf(runtime_dir_value, sizeof(runtime_dir_value), "%s", value);
                    } else if (SDL_strcmp(key, "TB_NPU_MODEL_DIR") == 0 || SDL_strcmp(key, "TB_NPU_PACKAGE_DIR") == 0) {
                        SDL_snprintf(package_dir_value, sizeof(package_dir_value), "%s", value);
                    } else if (SDL_strcmp(key, "TB_NPU_VOCAB_PATH") == 0 || SDL_strcmp(key, "TB_NPU_TOKENIZER_VOCAB_PATH") == 0) {
                        SDL_snprintf(tokenizer_vocab_value, sizeof(tokenizer_vocab_value), "%s", value);
                    } else if (SDL_strcmp(key, "TB_NPU_MEL_FILTERS_PATH") == 0 || SDL_strcmp(key, "TB_NPU_MEL_FILTER_PATH") == 0) {
                        SDL_snprintf(mel_filters_value, sizeof(mel_filters_value), "%s", value);
                    } else if (SDL_strcmp(key, "TB_NPU_CACHE_IDLE_MS") == 0 || SDL_strcmp(key, "TB_NPU_IDLE_CACHE_MS") == 0) {
                        SDL_snprintf(npu_cache_idle_ms_value, sizeof(npu_cache_idle_ms_value), "%s", value);
                    }
                }
            }

            line = strtok_s(NULL, "\r\n", &context);
        }

        trim_newlines(config->api_key);
        trim_newlines(backend_value);
        trim_newlines(openai_model_value);
        trim_newlines(npu_model_value);
        trim_newlines(runtime_dir_value);
        trim_newlines(package_dir_value);
        trim_newlines(tokenizer_vocab_value);
        trim_newlines(mel_filters_value);
        trim_newlines(npu_cache_idle_ms_value);
        if (npu_cache_idle_ms_value[0] != '\0') {
            char *end = NULL;
            unsigned long parsed = strtoul(npu_cache_idle_ms_value, &end, 10);

            if (end != npu_cache_idle_ms_value && end != NULL && *end == '\0') {
                config->npu_cache_idle_ms = (Uint32) parsed;
            }
        }
        config->backend = parse_backend_kind(backend_value);
        if (npu_model_value[0] != '\0') {
            SDL_snprintf(config->npu_model, sizeof(config->npu_model), "%s", npu_model_value);
        }
        if (config->backend == TB_TRANSCRIPTION_BACKEND_NPU) {
            if (config->npu_model[0] != '\0') {
                SDL_snprintf(config->model, sizeof(config->model), "%s", config->npu_model);
            }
        } else if (openai_model_value[0] != '\0') {
            SDL_snprintf(config->model, sizeof(config->model), "%s", openai_model_value);
        }
        trim_newlines(config->model);
        trim_newlines(config->npu_model);
        apply_backend_defaults(config);
        if (config->backend == TB_TRANSCRIPTION_BACKEND_NPU) {
            tb_transcription_backend_npu_configure(
                config,
                runtime_dir_value,
                package_dir_value,
                tokenizer_vocab_value,
                mel_filters_value
            );
            tb_transcription_backend_npu_probe(config);
        }
        SDL_free(contents);
    }

    SDL_free(raw);
}

bool tb_transcription_execute(
    const TbTranscriptionConfig *config,
    const TbTranscriptionRequest *request,
    char **out_text,
    char **out_error
) {
    if (out_text != NULL) {
        *out_text = NULL;
    }
    if (out_error != NULL) {
        *out_error = NULL;
    }

    if (config == NULL || request == NULL || request->samples == NULL || request->sample_count <= 0 || request->sample_rate <= 0) {
        if (out_error != NULL) {
            *out_error = SDL_strdup("Invalid transcription request.");
        }
        return false;
    }

    switch (config->backend) {
        case TB_TRANSCRIPTION_BACKEND_NPU:
            return tb_transcription_backend_npu_execute(config, request, out_text, out_error);
        case TB_TRANSCRIPTION_BACKEND_OPENAI:
        default:
            return tb_transcription_backend_openai_execute(config, request, out_text, out_error);
    }
}
