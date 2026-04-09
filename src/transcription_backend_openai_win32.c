#include "transcription_backend.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

static bool build_wav_buffer(const float *samples, int sample_count, int sample_rate, Uint8 **out_data, size_t *out_size) {
    size_t data_size = (size_t) sample_count * sizeof(int16_t);
    size_t total_size = 44 + data_size;
    Uint8 *buffer = (Uint8 *) SDL_malloc(total_size);
    if (buffer == NULL) {
        return false;
    }

    SDL_memset(buffer, 0, total_size);
    SDL_memcpy(buffer, "RIFF", 4);
    *(uint32_t *) (buffer + 4) = (uint32_t) (36 + data_size);
    SDL_memcpy(buffer + 8, "WAVE", 4);
    SDL_memcpy(buffer + 12, "fmt ", 4);
    *(uint32_t *) (buffer + 16) = 16;
    *(uint16_t *) (buffer + 20) = 1;
    *(uint16_t *) (buffer + 22) = 1;
    *(uint32_t *) (buffer + 24) = (uint32_t) sample_rate;
    *(uint32_t *) (buffer + 28) = (uint32_t) sample_rate * (uint32_t) sizeof(int16_t);
    *(uint16_t *) (buffer + 32) = sizeof(int16_t);
    *(uint16_t *) (buffer + 34) = 16;
    SDL_memcpy(buffer + 36, "data", 4);
    *(uint32_t *) (buffer + 40) = (uint32_t) data_size;

    for (int index = 0; index < sample_count; ++index) {
        float clamped = SDL_clamp(samples[index], -1.0f, 1.0f);
        int16_t pcm = (int16_t) (clamped * 32767.0f);
        *(int16_t *) (buffer + 44 + (index * (int) sizeof(int16_t))) = pcm;
    }

    *out_data = buffer;
    *out_size = total_size;
    return true;
}

static wchar_t *utf8_to_wide(const char *text) {
    int required = 0;
    wchar_t *buffer = NULL;

    required = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (required <= 0) {
        return NULL;
    }

    buffer = (wchar_t *) SDL_malloc((size_t) required * sizeof(wchar_t));
    if (buffer == NULL) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, buffer, required) <= 0) {
        SDL_free(buffer);
        return NULL;
    }

    return buffer;
}

static char *format_winhttp_error(const char *prefix) {
    char *message = NULL;
    DWORD code = GetLastError();
    SDL_asprintf(&message, "%s (WinHTTP error %lu)", prefix, (unsigned long) code);
    return message;
}

static void trim_newlines(char *text) {
    size_t length = SDL_strlen(text);
    while (length > 0 && (text[length - 1] == '\r' || text[length - 1] == '\n' || text[length - 1] == ' ' || text[length - 1] == '\t')) {
        text[length - 1] = '\0';
        --length;
    }
}

static bool read_winhttp_response(HINTERNET request, char **out_text) {
    char *buffer = NULL;
    size_t used = 0;

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            SDL_free(buffer);
            return false;
        }

        if (available == 0) {
            break;
        }

        char *grown = (char *) SDL_realloc(buffer, used + (size_t) available + 1);
        if (grown == NULL) {
            SDL_free(buffer);
            return false;
        }
        buffer = grown;

        DWORD read = 0;
        if (!WinHttpReadData(request, buffer + used, available, &read)) {
            SDL_free(buffer);
            return false;
        }

        used += read;
    }

    if (buffer == NULL) {
        buffer = (char *) SDL_malloc(1);
        if (buffer == NULL) {
            return false;
        }
        buffer[0] = '\0';
    } else {
        buffer[used] = '\0';
    }

    *out_text = buffer;
    return true;
}

static bool send_openai_request(
    const TbTranscriptionConfig *config,
    const TbTranscriptionRequest *request,
    const Uint8 *wav_data,
    size_t wav_size,
    char **out_text,
    char **out_error
) {
    const char *boundary = "----terminalbuddyboundary7MA4YWxkTrZu0gW";
    char *prefix = NULL;
    char *suffix = NULL;
    char *headers = NULL;
    wchar_t *headers_wide = NULL;
    Uint8 *body = NULL;
    size_t prefix_len = 0;
    size_t suffix_len = 0;
    size_t total_len = 0;
    HINTERNET session = NULL;
    HINTERNET connect = NULL;
    HINTERNET request_handle = NULL;
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    bool success = false;

    if (SDL_asprintf(
            &prefix,
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
            "%s\r\n"
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"
            "text\r\n"
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"prompt\"\r\n\r\n"
            "%s\r\n"
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
            "Content-Type: audio/wav\r\n\r\n",
            boundary,
            config->model,
            boundary,
            boundary,
            request->prompt,
            boundary
        ) < 0) {
        *out_error = SDL_strdup("Failed to allocate multipart prefix");
        goto cleanup;
    }

    if (SDL_asprintf(&suffix, "\r\n--%s--\r\n", boundary) < 0) {
        *out_error = SDL_strdup("Failed to allocate multipart suffix");
        goto cleanup;
    }

    prefix_len = SDL_strlen(prefix);
    suffix_len = SDL_strlen(suffix);
    total_len = prefix_len + wav_size + suffix_len;

    body = (Uint8 *) SDL_malloc(total_len);
    if (body == NULL) {
        *out_error = SDL_strdup("Failed to allocate multipart body");
        goto cleanup;
    }

    SDL_memcpy(body, prefix, prefix_len);
    SDL_memcpy(body + prefix_len, wav_data, wav_size);
    SDL_memcpy(body + prefix_len + wav_size, suffix, suffix_len);

    if (SDL_asprintf(
            &headers,
            "Authorization: Bearer %s\r\nContent-Type: multipart/form-data; boundary=%s\r\n",
            config->api_key,
            boundary
        ) < 0) {
        *out_error = SDL_strdup("Failed to allocate request headers");
        goto cleanup;
    }

    headers_wide = utf8_to_wide(headers);
    if (headers_wide == NULL) {
        *out_error = SDL_strdup("Failed to convert request headers");
        goto cleanup;
    }

    session = WinHttpOpen(L"terminal-buddy/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) {
        *out_error = format_winhttp_error("WinHttpOpen failed");
        goto cleanup;
    }

    connect = WinHttpConnect(session, L"api.openai.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect == NULL) {
        *out_error = format_winhttp_error("WinHttpConnect failed");
        goto cleanup;
    }

    request_handle = WinHttpOpenRequest(connect, L"POST", L"/v1/audio/transcriptions", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request_handle == NULL) {
        *out_error = format_winhttp_error("WinHttpOpenRequest failed");
        goto cleanup;
    }

    if (!WinHttpAddRequestHeaders(request_handle, headers_wide, (DWORD) -1L, WINHTTP_ADDREQ_FLAG_ADD)) {
        *out_error = format_winhttp_error("WinHttpAddRequestHeaders failed");
        goto cleanup;
    }

    if (!WinHttpSendRequest(request_handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, (DWORD) total_len, (DWORD) total_len, 0)) {
        *out_error = format_winhttp_error("WinHttpSendRequest failed");
        goto cleanup;
    }

    if (!WinHttpReceiveResponse(request_handle, NULL)) {
        *out_error = format_winhttp_error("WinHttpReceiveResponse failed");
        goto cleanup;
    }

    if (!WinHttpQueryHeaders(request_handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        *out_error = format_winhttp_error("WinHttpQueryHeaders failed");
        goto cleanup;
    }

    if (!read_winhttp_response(request_handle, out_text)) {
        *out_error = format_winhttp_error("Failed to read response");
        goto cleanup;
    }

    trim_newlines(*out_text);
    if (status_code < 200 || status_code >= 300) {
        SDL_asprintf(out_error, "OpenAI returned HTTP %lu: %s", (unsigned long) status_code, *out_text);
        SDL_free(*out_text);
        *out_text = NULL;
        goto cleanup;
    }

    success = true;

cleanup:
    if (request_handle != NULL) {
        WinHttpCloseHandle(request_handle);
    }
    if (connect != NULL) {
        WinHttpCloseHandle(connect);
    }
    if (session != NULL) {
        WinHttpCloseHandle(session);
    }
    SDL_free(headers_wide);
    SDL_free(headers);
    SDL_free(body);
    SDL_free(prefix);
    SDL_free(suffix);
    return success;
}

bool tb_transcription_backend_openai_execute(
    const TbTranscriptionConfig *config,
    const TbTranscriptionRequest *request,
    char **out_text,
    char **out_error
) {
    Uint8 *wav_data = NULL;
    size_t wav_size = 0;
    bool success = false;

    if (!build_wav_buffer(request->samples, request->sample_count, request->sample_rate, &wav_data, &wav_size)) {
        *out_error = SDL_strdup("Failed to build WAV payload");
        return false;
    }

    success = send_openai_request(config, request, wav_data, wav_size, out_text, out_error);
    SDL_free(wav_data);
    return success;
}

#else

bool tb_transcription_backend_openai_execute(
    const TbTranscriptionConfig *config,
    const TbTranscriptionRequest *request,
    char **out_text,
    char **out_error
) {
    (void) config;
    (void) request;
    (void) out_text;
    *out_error = SDL_strdup("OpenAI transcription is only wired for Windows in this scaffold.");
    return false;
}

#endif
