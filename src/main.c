#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "keyboard_emit_win32.h"
#include "keyboard_layout.h"
#include "keyboard_state.h"
#include "ui_clay.h"

#ifndef TERMINAL_BUDDY_SOURCE_DIR
#define TERMINAL_BUDDY_SOURCE_DIR "."
#endif

#define IDLE_WINDOW_SIZE 96
#define EXPANDED_WINDOW_WIDTH 360
#define EXPANDED_WINDOW_HEIGHT 120
#define KEYBOARD_LONG_PRESS_MS 325u
#define BUTTON_DIAMETER 76.0f
#define BUTTON_MARGIN 10.0f
#define DRAG_THRESHOLD 10.0f
#define AUDIO_HISTORY_COUNT 20
#define AUDIO_SAMPLE_RATE 24000
#define AUDIO_LEVEL_BOOST 3.5f
#define AUDIO_LEVEL_DECAY 0.88f
#define MAX_STATUS_TEXT 256
#define MAX_TRANSCRIPT_TEXT 2048
#define MAX_MODEL_NAME 64
#define MAX_API_KEY 512

typedef enum PointerKind {
    POINTER_NONE = 0,
    POINTER_MOUSE,
    POINTER_FINGER
} PointerKind;

typedef enum AppMode {
    APP_MODE_STANDARD = 0,
    APP_MODE_CODING,
    APP_MODE_TERMINAL
} AppMode;

typedef struct ModePalette {
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 glow_r;
    Uint8 glow_g;
    Uint8 glow_b;
} ModePalette;

typedef struct DragState {
    PointerKind kind;
    SDL_FingerID finger_id;
    bool pressed;
    bool dragging;
    bool pending_toggle;
    bool long_press_fired;
    float start_global_x;
    float start_global_y;
    int window_start_x;
    int window_start_y;
    Uint64 pressed_at_ms;
} DragState;

typedef struct KeyboardPressState {
    PointerKind kind;
    SDL_FingerID finger_id;
    bool pressed;
    bool armed;
    int key_index;
    Uint64 next_repeat_ms;
} KeyboardPressState;

typedef struct TrayState {
    SDL_Tray *tray;
    SDL_TrayMenu *menu;
    SDL_TrayMenu *mode_menu;
    SDL_TrayMenu *text_menu;
    SDL_TrayEntry *show_hide;
    SDL_TrayEntry *mode_parent;
    SDL_TrayEntry *mode_standard;
    SDL_TrayEntry *mode_coding;
    SDL_TrayEntry *mode_terminal;
    SDL_TrayEntry *text_parent;
    SDL_TrayEntry *text_surface;
    SDL_TrayEntry *text_engine;
    SDL_TrayEntry *text_debug;
    SDL_TrayEntry *quit;
    SDL_Surface *icon;
} TrayState;

typedef struct AudioState {
    SDL_AudioStream *stream;
    bool ready;
    float level;
    float history[AUDIO_HISTORY_COUNT];
    float *captured_samples;
    int captured_count;
    int captured_capacity;
} AudioState;

typedef struct TranscriptionState {
    SDL_Mutex *mutex;
    SDL_Thread *worker;
    bool processing;
    bool worker_done;
    bool show_feedback;
    bool clipboard_dirty;
    bool api_ready;
    char *env_path;
    char api_key[MAX_API_KEY];
    char model[MAX_MODEL_NAME];
    char status_text[MAX_STATUS_TEXT];
    char transcript_text[MAX_TRANSCRIPT_TEXT];
} TranscriptionState;

typedef struct WindowMetrics {
    int logical_width;
    int logical_height;
    int pixel_width;
    int pixel_height;
    float display_scale;
    float content_scale_x;
    float content_scale_y;
    float touch_scale;
    float ui_scale;
} WindowMetrics;

typedef struct AppState {
    bool running;
    bool listening;
    bool visible;
    bool needs_redraw;
    bool keyboard_visible;
    bool text_debug_logging;
    int window_x;
    int window_y;
    AppMode mode;
    TbUiTextRenderMode text_render_mode;
    Uint64 last_render_ms;
    char *prefs_path;
    SDL_Window *window;
    SDL_Renderer *renderer;
    DragState drag;
    KeyboardPressState keyboard_press;
    TbKeyboardModState keyboard_mods;
    TbKeyboardLayoutResult keyboard_layout;
    char keyboard_labels[TB_KEYBOARD_MAX_KEYS][8];
    WindowMetrics window_metrics;
    TrayState tray;
    AudioState audio;
    TranscriptionState transcription;
#ifdef _WIN32
    HWND app_hwnd;
    HWND last_external_hwnd;
    HWND injection_target_hwnd;
#endif
} AppState;

typedef struct TranscriptionJob {
    AppState *app;
    float *samples;
    int sample_count;
    AppMode mode;
    char *api_key;
    char *model;
} TranscriptionJob;

static SDL_LogOutputFunction g_default_log_output;
static void *g_default_log_userdata = NULL;
static SDL_Mutex *g_log_mutex = NULL;
static FILE *g_log_file = NULL;
static bool g_log_capture_enabled = false;
static char *g_log_path = NULL;

static int fail(const char *message) {
    SDL_Log("%s: %s", message, SDL_GetError());
    return 1;
}

static float squaref(float value) {
    return value * value;
}

static float button_center_x(const AppState *state) {
    return (BUTTON_MARGIN + (BUTTON_DIAMETER * 0.5f)) * state->window_metrics.ui_scale;
}

static float button_center_y(const AppState *state, bool listening) {
    if (listening) {
        return ((float) EXPANDED_WINDOW_HEIGHT * 0.5f) * state->window_metrics.ui_scale;
    }
    return ((float) IDLE_WINDOW_SIZE * 0.5f) * state->window_metrics.ui_scale;
}

static float keyboard_button_center_x(const AppState *state) {
    return state->keyboard_layout.bubble_rect.x + (state->keyboard_layout.bubble_rect.w * 0.5f);
}

static float keyboard_button_center_y(const AppState *state) {
    return state->keyboard_layout.bubble_rect.y + (state->keyboard_layout.bubble_rect.h * 0.5f);
}

static const char *mode_key(AppMode mode) {
    switch (mode) {
        case APP_MODE_STANDARD:
            return "standard";
        case APP_MODE_CODING:
            return "coding";
        case APP_MODE_TERMINAL:
            return "terminal";
        default:
            return "standard";
    }
}

static const char *mode_label(AppMode mode) {
    switch (mode) {
        case APP_MODE_STANDARD:
            return "Standard";
        case APP_MODE_CODING:
            return "Coding";
        case APP_MODE_TERMINAL:
            return "Terminal";
        default:
            return "Standard";
    }
}

static const char *mode_prompt(AppMode mode) {
    switch (mode) {
        case APP_MODE_STANDARD:
            return "This is spoken dictation. Remove filler words and false starts, add light punctuation, and preserve intent.";
        case APP_MODE_CODING:
            return "This is spoken dictation for coding agents like Codex and Claude. Preserve commands, flags, filenames, paths, repo names, code symbols, and technical terms exactly whenever possible. Remove filler words and false starts.";
        case APP_MODE_TERMINAL:
            return "This is spoken dictation for terminal work. Preserve shell commands, flags, filenames, paths, quoted text, and punctuation exactly whenever possible. Remove filler words and false starts.";
        default:
            return "This is spoken dictation. Remove filler words and false starts, add light punctuation, and preserve intent.";
    }
}

static const char *text_render_mode_key(TbUiTextRenderMode mode) {
    switch (mode) {
        case TB_UI_TEXT_RENDER_ENGINE:
            return "engine";
        case TB_UI_TEXT_RENDER_SURFACE:
        default:
            return "surface";
    }
}

static TbUiTextRenderMode parse_text_render_mode_key(const char *value) {
    if (value != NULL && SDL_strcmp(value, "engine") == 0) {
        return TB_UI_TEXT_RENDER_ENGINE;
    }
    return TB_UI_TEXT_RENDER_SURFACE;
}

static AppMode parse_mode_key(const char *value) {
    if (SDL_strcmp(value, "coding") == 0) {
        return APP_MODE_CODING;
    }
    if (SDL_strcmp(value, "terminal") == 0) {
        return APP_MODE_TERMINAL;
    }
    return APP_MODE_STANDARD;
}

static ModePalette get_mode_palette(AppMode mode) {
    switch (mode) {
        case APP_MODE_STANDARD:
            return (ModePalette) {42, 212, 138, 64, 79, 255};
        case APP_MODE_CODING:
            return (ModePalette) {82, 155, 255, 85, 111, 255};
        case APP_MODE_TERMINAL:
            return (ModePalette) {255, 171, 64, 255, 111, 76};
        default:
            return (ModePalette) {42, 212, 138, 64, 79, 255};
    }
}

static bool is_panel_expanded(const AppState *state) {
    return state->keyboard_visible || state->listening || state->transcription.processing || state->transcription.show_feedback;
}

static float current_touch_scale(const AppState *state) {
    if (state != NULL && state->window_metrics.touch_scale > 0.01f) {
        return state->window_metrics.touch_scale;
    }
    return 1.0f;
}

static int target_window_width(const AppState *state) {
    float touch_scale = current_touch_scale(state);

    if (state->keyboard_visible) {
        return (int) SDL_roundf((float) TB_KEYBOARD_WINDOW_WIDTH * touch_scale);
    }
    if (is_panel_expanded(state)) {
        return (int) SDL_roundf((float) EXPANDED_WINDOW_WIDTH * touch_scale);
    }
    return (int) SDL_roundf((float) IDLE_WINDOW_SIZE * touch_scale);
}

static int target_window_height(const AppState *state) {
    float touch_scale = current_touch_scale(state);

    if (state->keyboard_visible) {
        return (int) SDL_roundf((float) TB_KEYBOARD_WINDOW_HEIGHT * touch_scale);
    }
    if (is_panel_expanded(state)) {
        return (int) SDL_roundf((float) EXPANDED_WINDOW_HEIGHT * touch_scale);
    }
    return (int) SDL_roundf((float) IDLE_WINDOW_SIZE * touch_scale);
}

static float default_touch_scale_for_display(float display_scale) {
    float normalized = display_scale;

    if (normalized < 1.0f) {
        normalized = 1.0f;
    }
    if (normalized > 1.85f) {
        normalized = 1.85f;
    }

    return normalized;
}

static void sync_window_metrics(AppState *state) {
    int logical_width = target_window_width(state);
    int logical_height = target_window_height(state);
    int pixel_width = logical_width;
    int pixel_height = logical_height;
    float display_scale = 1.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float touch_scale = 1.0f;

    if (state == NULL || state->window == NULL) {
        return;
    }

    SDL_GetWindowSize(state->window, &logical_width, &logical_height);
    if (!SDL_GetWindowSizeInPixels(state->window, &pixel_width, &pixel_height)) {
        pixel_width = logical_width;
        pixel_height = logical_height;
    }
    display_scale = SDL_GetWindowDisplayScale(state->window);
    if (display_scale <= 0.01f) {
        display_scale = 1.0f;
    }
    touch_scale = default_touch_scale_for_display(display_scale);

    if (logical_width > 0 && pixel_width > 0) {
        scale_x = (float) pixel_width / (float) logical_width;
    }
    if (logical_height > 0 && pixel_height > 0) {
        scale_y = (float) pixel_height / (float) logical_height;
    }

    state->window_metrics.logical_width = logical_width;
    state->window_metrics.logical_height = logical_height;
    state->window_metrics.pixel_width = pixel_width;
    state->window_metrics.pixel_height = pixel_height;
    state->window_metrics.display_scale = display_scale;
    state->window_metrics.content_scale_x = scale_x > 0.01f ? scale_x : 1.0f;
    state->window_metrics.content_scale_y = scale_y > 0.01f ? scale_y : 1.0f;
    state->window_metrics.touch_scale = touch_scale;
    state->window_metrics.ui_scale = state->window_metrics.content_scale_x * state->window_metrics.touch_scale;
}

static float ui_x_from_window_x(const AppState *state, float window_x) {
    return window_x * state->window_metrics.content_scale_x;
}

static float ui_y_from_window_y(const AppState *state, float window_y) {
    return window_y * state->window_metrics.content_scale_y;
}

static float ui_x_from_normalized_x(const AppState *state, float normalized_x) {
    return normalized_x * (float) state->window_metrics.pixel_width;
}

static float ui_y_from_normalized_y(const AppState *state, float normalized_y) {
    return normalized_y * (float) state->window_metrics.pixel_height;
}

static void request_redraw(AppState *state) {
    if (state != NULL) {
        state->needs_redraw = true;
    }
}

static void trim_newlines(char *text) {
    size_t length = SDL_strlen(text);
    while (length > 0 && (text[length - 1] == '\r' || text[length - 1] == '\n' || text[length - 1] == ' ' || text[length - 1] == '\t')) {
        text[length - 1] = '\0';
        --length;
    }
}

static char *build_prefs_path(void) {
    char *pref_dir = SDL_GetPrefPath("ether", "terminal buddy");
    char *path = NULL;

    if (pref_dir == NULL) {
        SDL_Log("SDL_GetPrefPath failed: %s", SDL_GetError());
        return NULL;
    }

    if (SDL_asprintf(&path, "%ssettings.txt", pref_dir) < 0) {
        path = NULL;
    }

    SDL_free(pref_dir);
    return path;
}

static char *build_env_path(void) {
    char *path = NULL;
    if (SDL_asprintf(&path, "%s/.env/dev.env", TERMINAL_BUDDY_SOURCE_DIR) < 0) {
        return NULL;
    }
    return path;
}

static char *build_log_path(void) {
    char *path = NULL;

    if (SDL_CreateDirectory(TERMINAL_BUDDY_SOURCE_DIR "/logs")) {
        if (SDL_asprintf(&path, "%s/logs/text-debug.log", TERMINAL_BUDDY_SOURCE_DIR) < 0) {
            return NULL;
        }
    }

    return path;
}

static const char *log_priority_label(SDL_LogPriority priority) {
    switch (priority) {
        case SDL_LOG_PRIORITY_TRACE: return "TRACE";
        case SDL_LOG_PRIORITY_VERBOSE: return "VERBOSE";
        case SDL_LOG_PRIORITY_DEBUG: return "DEBUG";
        case SDL_LOG_PRIORITY_INFO: return "INFO";
        case SDL_LOG_PRIORITY_WARN: return "WARN";
        case SDL_LOG_PRIORITY_ERROR: return "ERROR";
        case SDL_LOG_PRIORITY_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

static void SDLCALL terminal_buddy_log_output(void *userdata, int category, SDL_LogPriority priority, const char *message) {
    (void) userdata;

    if (g_default_log_output != NULL) {
        g_default_log_output(g_default_log_userdata, category, priority, message);
    }

    if (!g_log_capture_enabled || g_log_file == NULL || message == NULL) {
        return;
    }

    if (g_log_mutex != NULL) {
        SDL_LockMutex(g_log_mutex);
    }

    fprintf(g_log_file, "[%s][cat=%d] %s\n", log_priority_label(priority), category, message);
    fflush(g_log_file);

    if (g_log_mutex != NULL) {
        SDL_UnlockMutex(g_log_mutex);
    }
}

static void set_log_capture_enabled(bool enabled) {
    g_log_capture_enabled = enabled;

    if (!enabled) {
        return;
    }

    if (g_log_path == NULL) {
        g_log_path = build_log_path();
    }
    if (g_log_file == NULL && g_log_path != NULL) {
        fopen_s(&g_log_file, g_log_path, "a");
    }
    if (g_log_file != NULL) {
        fprintf(g_log_file, "---- text debug session ----\n");
        fflush(g_log_file);
    }
}

static void shutdown_log_capture(void) {
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    if (g_log_mutex != NULL) {
        SDL_DestroyMutex(g_log_mutex);
        g_log_mutex = NULL;
    }
    SDL_free(g_log_path);
    g_log_path = NULL;
}

static void transcription_set_ui(
    AppState *state,
    bool processing,
    bool show_feedback,
    bool clipboard_dirty,
    const char *status,
    const char *transcript
) {
    SDL_LockMutex(state->transcription.mutex);
    state->transcription.processing = processing;
    state->transcription.show_feedback = show_feedback;
    if (clipboard_dirty) {
        state->transcription.clipboard_dirty = true;
    }

    if (status != NULL) {
        SDL_snprintf(state->transcription.status_text, sizeof(state->transcription.status_text), "%s", status);
    }
    if (transcript != NULL) {
        SDL_snprintf(state->transcription.transcript_text, sizeof(state->transcription.transcript_text), "%s", transcript);
    }
    SDL_UnlockMutex(state->transcription.mutex);
}

static void transcription_clear_feedback(AppState *state) {
    SDL_LockMutex(state->transcription.mutex);
    state->transcription.show_feedback = false;
    state->transcription.status_text[0] = '\0';
    state->transcription.transcript_text[0] = '\0';
    SDL_UnlockMutex(state->transcription.mutex);
}

static void copy_transcription_snapshot(
    AppState *state,
    bool *processing,
    bool *show_feedback,
    char *status_out,
    size_t status_size,
    char *transcript_out,
    size_t transcript_size
) {
    SDL_LockMutex(state->transcription.mutex);
    *processing = state->transcription.processing;
    *show_feedback = state->transcription.show_feedback;
    SDL_snprintf(status_out, status_size, "%s", state->transcription.status_text);
    SDL_snprintf(transcript_out, transcript_size, "%s", state->transcription.transcript_text);
    SDL_UnlockMutex(state->transcription.mutex);
}

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

static void reload_dev_env(AppState *state) {
    size_t size = 0;
    void *raw = NULL;

    state->transcription.api_ready = false;
    state->transcription.api_key[0] = '\0';
    SDL_snprintf(state->transcription.model, sizeof(state->transcription.model), "%s", "gpt-4o-transcribe");

    if (state->transcription.env_path == NULL) {
        state->transcription.env_path = build_env_path();
    }
    if (state->transcription.env_path == NULL) {
        return;
    }

    raw = SDL_LoadFile(state->transcription.env_path, &size);
    if (raw != NULL) {
        char *contents = (char *) SDL_malloc(size + 1);
        if (contents != NULL) {
            char *context = NULL;
            char *line = NULL;

            SDL_memcpy(contents, raw, size);
            contents[size] = '\0';

            line = strtok_s(contents, "\r\n", &context);
            while (line != NULL) {
                char *key = NULL;
                char *value = NULL;

                if (line[0] != '#' && line[0] != '\0') {
                    parse_env_assignment(line, &key, &value);
                    if (key != NULL && value != NULL) {
                        if (SDL_strcmp(key, "OPENAI_API_KEY") == 0) {
                            SDL_snprintf(state->transcription.api_key, sizeof(state->transcription.api_key), "%s", value);
                        } else if (SDL_strcmp(key, "OPENAI_TRANSCRIBE_MODEL") == 0) {
                            SDL_snprintf(state->transcription.model, sizeof(state->transcription.model), "%s", value);
                        }
                    }
                }

                line = strtok_s(NULL, "\r\n", &context);
            }

            SDL_free(contents);
        }

        SDL_free(raw);
    }

    trim_newlines(state->transcription.api_key);
    trim_newlines(state->transcription.model);
    state->transcription.api_ready = state->transcription.api_key[0] != '\0';
}

static void save_preferences(const AppState *state) {
    char *contents = NULL;

    if (state->prefs_path == NULL) {
        return;
    }

    if (SDL_asprintf(
            &contents,
            "window_x=%d\nwindow_y=%d\nmode=%s\ntext_mode=%s\ntext_debug=%d\n",
            state->window_x,
            state->window_y,
            mode_key(state->mode),
            text_render_mode_key(state->text_render_mode),
            state->text_debug_logging ? 1 : 0
        ) < 0) {
        SDL_Log("SDL_asprintf failed while saving preferences");
        return;
    }

    if (!SDL_SaveFile(state->prefs_path, contents, SDL_strlen(contents))) {
        SDL_Log("SDL_SaveFile failed: %s", SDL_GetError());
    }

    SDL_free(contents);
}

static void load_preferences(AppState *state) {
    size_t size = 0;
    void *raw = NULL;

    state->window_x = 64;
    state->window_y = 64;
    state->mode = APP_MODE_STANDARD;
    state->text_render_mode = TB_UI_TEXT_RENDER_SURFACE;
    state->text_debug_logging = false;

    state->prefs_path = build_prefs_path();
    if (state->prefs_path == NULL) {
        return;
    }

    raw = SDL_LoadFile(state->prefs_path, &size);
    if (raw != NULL) {
        char *contents = (char *) SDL_malloc(size + 1);
        if (contents != NULL) {
            char *context = NULL;
            char *line = NULL;

            SDL_memcpy(contents, raw, size);
            contents[size] = '\0';

            line = strtok_s(contents, "\r\n", &context);
            while (line != NULL) {
                if (SDL_strncmp(line, "window_x=", 9) == 0) {
                    state->window_x = atoi(line + 9);
                } else if (SDL_strncmp(line, "window_y=", 9) == 0) {
                    state->window_y = atoi(line + 9);
                } else if (SDL_strncmp(line, "mode=", 5) == 0) {
                    state->mode = parse_mode_key(line + 5);
                } else if (SDL_strncmp(line, "text_mode=", 10) == 0) {
                    state->text_render_mode = parse_text_render_mode_key(line + 10);
                } else if (SDL_strncmp(line, "text_debug=", 11) == 0) {
                    state->text_debug_logging = atoi(line + 11) != 0;
                }

                line = strtok_s(NULL, "\r\n", &context);
            }

            SDL_free(contents);
        }

        SDL_free(raw);
    }
}

static bool resize_for_mode(AppState *state) {
    int width = target_window_width(state);
    int height = target_window_height(state);

    if (!SDL_SetWindowSize(state->window, width, height)) {
        SDL_Log("SDL_SetWindowSize failed: %s", SDL_GetError());
        return false;
    }

    sync_window_metrics(state);
    return true;
}

static void clear_audio_visuals(AppState *state) {
    state->audio.level = 0.0f;
    SDL_zeroa(state->audio.history);
}

static void clear_captured_audio(AppState *state) {
    state->audio.captured_count = 0;
}

static bool ensure_capture_capacity(AppState *state, int additional_samples) {
    int needed = state->audio.captured_count + additional_samples;
    if (needed <= state->audio.captured_capacity) {
        return true;
    }

    int new_capacity = state->audio.captured_capacity > 0 ? state->audio.captured_capacity : 4096;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    float *new_buffer = (float *) SDL_realloc(state->audio.captured_samples, sizeof(float) * (size_t) new_capacity);
    if (new_buffer == NULL) {
        SDL_Log("Failed to grow capture buffer");
        return false;
    }

    state->audio.captured_samples = new_buffer;
    state->audio.captured_capacity = new_capacity;
    return true;
}

static void push_audio_level(AppState *state, float level) {
    SDL_memmove(
        state->audio.history,
        state->audio.history + 1,
        sizeof(state->audio.history[0]) * (AUDIO_HISTORY_COUNT - 1)
    );
    state->audio.history[AUDIO_HISTORY_COUNT - 1] = level;
}

static bool initialize_audio(AppState *state) {
    SDL_AudioSpec desired_spec;

    SDL_zero(desired_spec);
    desired_spec.format = SDL_AUDIO_F32;
    desired_spec.channels = 1;
    desired_spec.freq = AUDIO_SAMPLE_RATE;

    state->audio.stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_RECORDING,
        &desired_spec,
        NULL,
        NULL
    );
    if (state->audio.stream == NULL) {
        SDL_Log("SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        state->audio.ready = false;
        return false;
    }

    state->audio.ready = true;
    clear_audio_visuals(state);
    return true;
}

static void destroy_audio(AppState *state) {
    if (state->audio.stream != NULL) {
        SDL_DestroyAudioStream(state->audio.stream);
        state->audio.stream = NULL;
    }

    SDL_free(state->audio.captured_samples);
    state->audio.captured_samples = NULL;
    state->audio.captured_capacity = 0;
    state->audio.captured_count = 0;
    state->audio.ready = false;
}

static void update_audio_meter(AppState *state) {
    float samples[1024];
    float peak = 0.0f;
    bool observed = false;

    if (!state->listening || !state->audio.ready || state->audio.stream == NULL) {
        return;
    }

    for (;;) {
        int available = SDL_GetAudioStreamAvailable(state->audio.stream);
        if (available <= 0) {
            break;
        }

        int requested = SDL_min(available, (int) sizeof(samples));
        int received = SDL_GetAudioStreamData(state->audio.stream, samples, requested);
        if (received <= 0) {
            break;
        }

        int sample_count = received / (int) sizeof(float);
        observed = true;

        if (ensure_capture_capacity(state, sample_count)) {
            SDL_memcpy(
                state->audio.captured_samples + state->audio.captured_count,
                samples,
                (size_t) received
            );
            state->audio.captured_count += sample_count;
        }

        for (int index = 0; index < sample_count; ++index) {
            float magnitude = SDL_fabsf(samples[index]);
            if (magnitude > peak) {
                peak = magnitude;
            }
        }
    }

    if (observed) {
        float boosted = SDL_min(peak * AUDIO_LEVEL_BOOST, 1.0f);
        state->audio.level = (state->audio.level * 0.55f) + (boosted * 0.45f);
    } else {
        state->audio.level *= AUDIO_LEVEL_DECAY;
    }

    if (state->audio.level < 0.01f) {
        state->audio.level = 0.0f;
    }

    push_audio_level(state, state->audio.level);
}

static void update_tray_checks(AppState *state) {
    if (state->tray.mode_standard != NULL) {
        SDL_SetTrayEntryChecked(state->tray.mode_standard, state->mode == APP_MODE_STANDARD);
    }
    if (state->tray.mode_coding != NULL) {
        SDL_SetTrayEntryChecked(state->tray.mode_coding, state->mode == APP_MODE_CODING);
    }
    if (state->tray.mode_terminal != NULL) {
        SDL_SetTrayEntryChecked(state->tray.mode_terminal, state->mode == APP_MODE_TERMINAL);
    }
    if (state->tray.text_surface != NULL) {
        SDL_SetTrayEntryChecked(state->tray.text_surface, state->text_render_mode == TB_UI_TEXT_RENDER_SURFACE);
    }
    if (state->tray.text_engine != NULL) {
        SDL_SetTrayEntryChecked(state->tray.text_engine, state->text_render_mode == TB_UI_TEXT_RENDER_ENGINE);
    }
    if (state->tray.text_debug != NULL) {
        SDL_SetTrayEntryChecked(state->tray.text_debug, state->text_debug_logging);
    }
}

static void update_tray_icon(AppState *state) {
    SDL_Rect shell = {3, 3, 26, 26};
    SDL_Rect inner = {7, 7, 18, 18};
    ModePalette palette = get_mode_palette(state->mode);
    Uint32 transparent = 0;
    Uint32 shell_color = 0;
    Uint32 accent_color = 0;

    if (state->tray.icon == NULL) {
        return;
    }

    transparent = SDL_MapSurfaceRGBA(state->tray.icon, 0, 0, 0, 0);
    shell_color = SDL_MapSurfaceRGBA(state->tray.icon, 18, 22, 31, 255);
    accent_color = SDL_MapSurfaceRGBA(state->tray.icon, palette.r, palette.g, palette.b, 255);

    SDL_FillSurfaceRect(state->tray.icon, NULL, transparent);
    SDL_FillSurfaceRect(state->tray.icon, &shell, shell_color);
    SDL_FillSurfaceRect(state->tray.icon, &inner, accent_color);

    if (state->tray.tray != NULL) {
        SDL_SetTrayIcon(state->tray.tray, state->tray.icon);
    }
}

static void update_tray_labels(AppState *state) {
    char tooltip[128];

    if (state->tray.show_hide != NULL) {
        SDL_SetTrayEntryLabel(state->tray.show_hide, state->visible ? "Hide Button" : "Show Button");
    }

    if (state->tray.tray != NULL) {
        SDL_snprintf(
            tooltip,
            sizeof(tooltip),
            "terminal-buddy (%s mode, %s text)",
            mode_label(state->mode)
            ,
            state->text_render_mode == TB_UI_TEXT_RENDER_ENGINE ? "engine" : "surface"
        );
        SDL_SetTrayTooltip(state->tray.tray, tooltip);
    }
}

static void refresh_shell_state(AppState *state) {
    update_tray_checks(state);
    update_tray_icon(state);
    update_tray_labels(state);
    request_redraw(state);
}

static void set_text_render_mode(AppState *state, TbUiTextRenderMode mode, bool persist) {
    state->text_render_mode = mode;
    tb_ui_set_text_render_mode(mode);
    refresh_shell_state(state);

    if (persist) {
        save_preferences(state);
    }
}

static void set_text_debug_logging(AppState *state, bool enabled, bool persist) {
    state->text_debug_logging = enabled;
    tb_ui_set_text_debug_logging(enabled);
    set_log_capture_enabled(enabled);
    refresh_shell_state(state);

    if (persist) {
        save_preferences(state);
    }
}

static void clear_keyboard_press(AppState *state) {
    state->keyboard_press.kind = POINTER_NONE;
    state->keyboard_press.finger_id = 0;
    state->keyboard_press.pressed = false;
    state->keyboard_press.armed = false;
    state->keyboard_press.key_index = -1;
    state->keyboard_press.next_repeat_ms = 0;
}

static void hide_keyboard(AppState *state) {
    state->keyboard_visible = false;
    clear_keyboard_press(state);
    tb_keyboard_mod_init(&state->keyboard_mods);
    resize_for_mode(state);
    request_redraw(state);
}

static void show_keyboard(AppState *state) {
    if (state->listening || state->transcription.processing) {
        return;
    }

    transcription_clear_feedback(state);
    state->keyboard_visible = true;
    clear_keyboard_press(state);
#ifdef _WIN32
    state->injection_target_hwnd = state->last_external_hwnd;
#endif
    resize_for_mode(state);
    tb_keyboard_build_layout(
        state->window_metrics.pixel_width,
        state->window_metrics.pixel_height,
        state->window_metrics.ui_scale,
        &state->keyboard_layout
    );
    request_redraw(state);
}

static void collapse_panel(AppState *state) {
    transcription_clear_feedback(state);
    clear_audio_visuals(state);
    resize_for_mode(state);
    request_redraw(state);
}

static void stop_listening(AppState *state) {
    if (state->audio.ready && state->audio.stream != NULL) {
        SDL_PauseAudioStreamDevice(state->audio.stream);
        SDL_ClearAudioStream(state->audio.stream);
    }

    state->listening = false;
    clear_audio_visuals(state);
    request_redraw(state);
}

static void start_listening(AppState *state) {
    if (state->transcription.processing) {
        return;
    }

    if (state->keyboard_visible) {
        hide_keyboard(state);
    }

    if (!state->audio.ready || state->audio.stream == NULL) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "MICROPHONE UNAVAILABLE",
            "SDL could not open the default recording device."
        );
        resize_for_mode(state);
        return;
    }

    transcription_clear_feedback(state);
    clear_audio_visuals(state);
    clear_captured_audio(state);
#ifdef _WIN32
    state->injection_target_hwnd = state->last_external_hwnd;
#endif
    SDL_ClearAudioStream(state->audio.stream);
    if (!SDL_ResumeAudioStreamDevice(state->audio.stream)) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "MIC START FAILED",
            SDL_GetError()
        );
        resize_for_mode(state);
        request_redraw(state);
        return;
    }

    state->listening = true;
    resize_for_mode(state);
    request_redraw(state);
}

static void set_window_visibility(AppState *state, bool visible) {
    if (state->visible == visible) {
        return;
    }

    state->visible = visible;
    if (visible) {
        SDL_ShowWindow(state->window);
        SDL_RaiseWindow(state->window);
        SDL_SyncWindow(state->window);
    } else {
        if (state->listening) {
            stop_listening(state);
        }
        if (state->keyboard_visible) {
            hide_keyboard(state);
        }
        SDL_HideWindow(state->window);
        SDL_SyncWindow(state->window);
    }

    update_tray_labels(state);
    request_redraw(state);
}

static void set_mode(AppState *state, AppMode mode, bool persist) {
    if (state->mode == mode) {
        update_tray_checks(state);
        return;
    }

    state->mode = mode;
    refresh_shell_state(state);

    if (persist) {
        save_preferences(state);
    }

    request_redraw(state);
}

static bool point_in_circle(float x, float y, float center_x, float center_y, float radius) {
    float dx = x - center_x;
    float dy = y - center_y;
    return (squaref(dx) + squaref(dy)) <= squaref(radius);
}

static bool point_in_active_surface(const AppState *state, float x, float y) {
    if (state->keyboard_visible) {
        return x >= 0.0f
            && y >= 0.0f
            && x <= (float) state->window_metrics.pixel_width
            && y <= (float) state->window_metrics.pixel_height;
    }

    if (is_panel_expanded(state)) {
        return x >= 0.0f
            && y >= 0.0f
            && x <= (float) state->window_metrics.pixel_width
            && y <= (float) state->window_metrics.pixel_height;
    }

    return point_in_circle(
        x,
        y,
        button_center_x(state),
        button_center_y(state, false),
        (BUTTON_DIAMETER * 0.5f) * state->window_metrics.ui_scale
    );
}

static const TbKeyboardKeyRect *keyboard_hit_at(AppState *state, float local_x, float local_y) {
    return tb_keyboard_hit_test(&state->keyboard_layout, local_x, local_y);
}

static bool emit_keyboard_spec(AppState *state, const TbKeyboardKeySpec *spec) {
    bool emitted = false;

    if (spec == NULL) {
        return false;
    }

    switch (spec->action) {
        case TB_KEYBOARD_ACTION_SHIFT:
            tb_keyboard_toggle_shift(&state->keyboard_mods, SDL_GetTicks());
            request_redraw(state);
            return true;
        case TB_KEYBOARD_ACTION_CTRL:
            tb_keyboard_toggle_ctrl(&state->keyboard_mods);
            request_redraw(state);
            return true;
        case TB_KEYBOARD_ACTION_ALT:
            tb_keyboard_toggle_alt(&state->keyboard_mods);
            request_redraw(state);
            return true;
        default:
            break;
    }

#ifdef _WIN32
    if (state->injection_target_hwnd == NULL || !IsWindow(state->injection_target_hwnd)) {
        state->injection_target_hwnd = state->last_external_hwnd;
    }
    emitted = tb_keyboard_emit_key(state->injection_target_hwnd, spec, &state->keyboard_mods);
#else
    emitted = false;
#endif

    tb_keyboard_auto_release(&state->keyboard_mods);
    request_redraw(state);
    return emitted;
}

static void begin_keyboard_press(AppState *state, PointerKind kind, SDL_FingerID finger_id, int key_index) {
    state->keyboard_press.kind = kind;
    state->keyboard_press.finger_id = finger_id;
    state->keyboard_press.pressed = true;
    state->keyboard_press.armed = true;
    state->keyboard_press.key_index = key_index;
    state->keyboard_press.next_repeat_ms = SDL_GetTicks() + TB_KEYBOARD_REPEAT_INITIAL_MS;
    request_redraw(state);
}

static void update_keyboard_press(AppState *state, float local_x, float local_y) {
    const TbKeyboardKeyRect *hit = NULL;

    if (!state->keyboard_press.pressed) {
        return;
    }

    hit = keyboard_hit_at(state, local_x, local_y);
    state->keyboard_press.armed = hit != NULL && hit->index == state->keyboard_press.key_index;
    request_redraw(state);
}

static void end_keyboard_press(AppState *state) {
    const TbKeyboardKeyRect *key = tb_keyboard_key_rect_at(&state->keyboard_layout, state->keyboard_press.key_index);
    bool should_emit = state->keyboard_press.pressed && state->keyboard_press.armed && key != NULL;

    clear_keyboard_press(state);

    if (should_emit) {
        emit_keyboard_spec(state, key->spec);
    } else {
        request_redraw(state);
    }
}

static void pump_keyboard_repeat(AppState *state) {
    const TbKeyboardKeyRect *key = NULL;
    Uint64 now_ms = 0;

    if (!state->keyboard_visible || !state->keyboard_press.pressed || !state->keyboard_press.armed) {
        return;
    }

    key = tb_keyboard_key_rect_at(&state->keyboard_layout, state->keyboard_press.key_index);
    if (key == NULL || key->spec == NULL || !key->spec->repeatable) {
        return;
    }

    now_ms = SDL_GetTicks();
    while (state->keyboard_press.next_repeat_ms > 0 && now_ms >= state->keyboard_press.next_repeat_ms) {
        emit_keyboard_spec(state, key->spec);
        state->keyboard_press.next_repeat_ms += TB_KEYBOARD_REPEAT_INTERVAL_MS;
    }
}

static void begin_pointer_interaction(
    AppState *state,
    PointerKind kind,
    SDL_FingerID finger_id,
    float local_x,
    float local_y,
    float global_x,
    float global_y
) {
    if (state->keyboard_visible) {
        if (point_in_circle(
            local_x,
            local_y,
            keyboard_button_center_x(state),
            keyboard_button_center_y(state),
            state->keyboard_layout.bubble_rect.w * 0.5f
        )) {
            state->drag.kind = kind;
            state->drag.finger_id = finger_id;
            state->drag.pressed = true;
            state->drag.dragging = false;
            state->drag.pending_toggle = true;
            state->drag.long_press_fired = false;
            state->drag.start_global_x = global_x;
            state->drag.start_global_y = global_y;
            state->drag.window_start_x = state->window_x;
            state->drag.window_start_y = state->window_y;
            state->drag.pressed_at_ms = SDL_GetTicks();
            return;
        }

        const TbKeyboardKeyRect *key = keyboard_hit_at(state, local_x, local_y);
        if (key != NULL) {
            begin_keyboard_press(state, kind, finger_id, key->index);
        }
        return;
    }

    if (!point_in_active_surface(state, local_x, local_y)) {
        return;
    }

    state->drag.kind = kind;
    state->drag.finger_id = finger_id;
    state->drag.pressed = true;
    state->drag.dragging = false;
    state->drag.pending_toggle = true;
    state->drag.long_press_fired = false;
    state->drag.start_global_x = global_x;
    state->drag.start_global_y = global_y;
    state->drag.window_start_x = state->window_x;
    state->drag.window_start_y = state->window_y;
    state->drag.pressed_at_ms = SDL_GetTicks();
}

static void update_pointer_interaction(AppState *state, float global_x, float global_y) {
    float delta_x = 0.0f;
    float delta_y = 0.0f;

    if (!state->drag.pressed) {
        return;
    }

    delta_x = global_x - state->drag.start_global_x;
    delta_y = global_y - state->drag.start_global_y;

    if (!state->drag.dragging) {
        float moved = squaref(delta_x) + squaref(delta_y);
        if (moved >= squaref(DRAG_THRESHOLD * state->window_metrics.ui_scale)) {
            state->drag.dragging = true;
            state->drag.pending_toggle = false;
        }
    }

    if (state->drag.dragging) {
        state->window_x = state->drag.window_start_x + (int) SDL_roundf(delta_x);
        state->window_y = state->drag.window_start_y + (int) SDL_roundf(delta_y);
        SDL_SetWindowPosition(state->window, state->window_x, state->window_y);
        request_redraw(state);
    }
}

static void handle_panel_tap(AppState *state);

static void end_pointer_interaction(AppState *state) {
    bool should_toggle = state->drag.pressed && state->drag.pending_toggle && !state->drag.dragging;
    bool moved = state->drag.dragging;

    state->drag.kind = POINTER_NONE;
    state->drag.finger_id = 0;
    state->drag.pressed = false;
    state->drag.dragging = false;
    state->drag.pending_toggle = false;
    state->drag.long_press_fired = false;
    state->drag.pressed_at_ms = 0;

    if (should_toggle) {
        handle_panel_tap(state);
    }

    if (moved) {
        save_preferences(state);
    }

    request_redraw(state);
}

static void pump_bubble_long_press(AppState *state) {
    Uint64 now_ms = 0;

    if (
        state->keyboard_visible
        || state->listening
        || state->transcription.processing
        || state->transcription.show_feedback
        || state->drag.long_press_fired
        || !state->drag.pressed
        || state->drag.dragging
        || !state->drag.pending_toggle
    ) {
        return;
    }

    now_ms = SDL_GetTicks();
    if (state->drag.pressed_at_ms == 0 || now_ms - state->drag.pressed_at_ms < KEYBOARD_LONG_PRESS_MS) {
        return;
    }

    state->drag.long_press_fired = true;
    state->drag.pending_toggle = false;
    show_keyboard(state);
}

static void render(AppState *state) {
    Uint64 ticks_ms = SDL_GetTicks();
    float pulse = 0.5f + (0.5f * SDL_sinf((float) ticks_ms * 0.0065f));
    bool processing = false;
    bool show_feedback = false;
    char status[MAX_STATUS_TEXT];
    char transcript[MAX_TRANSCRIPT_TEXT];
    TbUiModel model;

    SDL_zero(model);

    copy_transcription_snapshot(
        state,
        &processing,
        &show_feedback,
        status,
        sizeof(status),
        transcript,
        sizeof(transcript)
    );

    sync_window_metrics(state);
    model.window_width = state->window_metrics.pixel_width;
    model.window_height = state->window_metrics.pixel_height;
    model.ui_scale = state->window_metrics.ui_scale;

    if (state->keyboard_visible) {
        tb_keyboard_build_layout(model.window_width, model.window_height, model.ui_scale, &state->keyboard_layout);
        for (int index = 0; index < state->keyboard_layout.key_count && index < TB_KEYBOARD_MAX_KEYS; ++index) {
            const TbKeyboardKeyRect *key = &state->keyboard_layout.keys[index];
            const char *label = tb_keyboard_display_label(
                key->spec,
                &state->keyboard_mods,
                state->keyboard_labels[index],
                sizeof(state->keyboard_labels[index])
            );
            if (label != state->keyboard_labels[index]) {
                SDL_snprintf(state->keyboard_labels[index], sizeof(state->keyboard_labels[index]), "%s", label);
            }
        }
        model.scene = TB_UI_SCENE_KEYBOARD;
    } else if (state->listening) {
        model.scene = TB_UI_SCENE_LISTENING;
    } else if (processing) {
        model.scene = TB_UI_SCENE_PROCESSING;
    } else if (show_feedback) {
        model.scene = TB_UI_SCENE_FEEDBACK;
    } else {
        model.scene = TB_UI_SCENE_IDLE;
    }
    model.mode = (int) state->mode;
    model.pulse = pulse;
    model.audio_level = state->audio.level;
    model.ticks_ms = ticks_ms;
    model.audio_history = state->audio.history;
    model.audio_history_count = AUDIO_HISTORY_COUNT;
    model.mode_label = mode_label(state->mode);
    model.status_text = status[0] != '\0' ? status : NULL;
    model.transcript_text = transcript[0] != '\0' ? transcript : NULL;
    model.keyboard_layout = state->keyboard_visible ? &state->keyboard_layout : NULL;
    model.keyboard_mods = &state->keyboard_mods;
    model.keyboard_labels = state->keyboard_labels;
    model.keyboard_pressed_key = state->keyboard_press.pressed && state->keyboard_press.armed ? state->keyboard_press.key_index : -1;

    if (!tb_ui_render(&model)) {
        SDL_Log("tb_ui_render failed");
    }

    state->needs_redraw = false;
    state->last_render_ms = ticks_ms;
}

static void handle_mouse_down(AppState *state, const SDL_MouseButtonEvent *button) {
    float global_x = 0.0f;
    float global_y = 0.0f;

    if (button->button != SDL_BUTTON_LEFT || state->drag.pressed) {
        return;
    }

    SDL_GetGlobalMouseState(&global_x, &global_y);
    begin_pointer_interaction(
        state,
        POINTER_MOUSE,
        0,
        ui_x_from_window_x(state, button->x),
        ui_y_from_window_y(state, button->y),
        global_x,
        global_y
    );
}

static void handle_mouse_motion(AppState *state) {
    float global_x = 0.0f;
    float global_y = 0.0f;
    float local_x = 0.0f;
    float local_y = 0.0f;

    if (state->keyboard_press.kind == POINTER_MOUSE) {
        SDL_GetMouseState(&local_x, &local_y);
        update_keyboard_press(state, ui_x_from_window_x(state, local_x), ui_y_from_window_y(state, local_y));
        return;
    }

    if (state->drag.kind != POINTER_MOUSE) {
        return;
    }

    SDL_GetGlobalMouseState(&global_x, &global_y);
    update_pointer_interaction(state, global_x, global_y);
}

static void handle_mouse_up(AppState *state, const SDL_MouseButtonEvent *button) {
    if (button->button != SDL_BUTTON_LEFT) {
        return;
    }

    if (state->keyboard_press.kind == POINTER_MOUSE) {
        end_keyboard_press(state);
        return;
    }

    if (state->drag.kind != POINTER_MOUSE) {
        return;
    }

    end_pointer_interaction(state);
}

static void handle_finger_down(AppState *state, const SDL_TouchFingerEvent *finger) {
    float local_x = 0.0f;
    float local_y = 0.0f;

    if (state->drag.pressed) {
        return;
    }

    local_x = ui_x_from_normalized_x(state, finger->x);
    local_y = ui_y_from_normalized_y(state, finger->y);

    begin_pointer_interaction(
        state,
        POINTER_FINGER,
        finger->fingerID,
        local_x,
        local_y,
        (float) state->window_x + local_x,
        (float) state->window_y + local_y
    );
}

static void handle_finger_motion(AppState *state, const SDL_TouchFingerEvent *finger) {
    float local_x = 0.0f;
    float local_y = 0.0f;

    local_x = ui_x_from_normalized_x(state, finger->x);
    local_y = ui_y_from_normalized_y(state, finger->y);

    if (state->keyboard_press.kind == POINTER_FINGER && state->keyboard_press.finger_id == finger->fingerID) {
        update_keyboard_press(state, local_x, local_y);
        return;
    }

    if (state->drag.kind != POINTER_FINGER || state->drag.finger_id != finger->fingerID) {
        return;
    }

    update_pointer_interaction(
        state,
        (float) state->window_x + local_x,
        (float) state->window_y + local_y
    );
}

static void handle_finger_up(AppState *state, const SDL_TouchFingerEvent *finger) {
    if (state->keyboard_press.kind == POINTER_FINGER && state->keyboard_press.finger_id == finger->fingerID) {
        end_keyboard_press(state);
        return;
    }

    if (state->drag.kind != POINTER_FINGER || state->drag.finger_id != finger->fingerID) {
        return;
    }

    end_pointer_interaction(state);
}

static void SDLCALL on_tray_show_hide(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    set_window_visibility(state, !state->visible);
}

static void SDLCALL on_tray_mode_standard(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    set_mode(state, APP_MODE_STANDARD, true);
}

static void SDLCALL on_tray_mode_coding(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    set_mode(state, APP_MODE_CODING, true);
}

static void SDLCALL on_tray_mode_terminal(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    set_mode(state, APP_MODE_TERMINAL, true);
}

static void SDLCALL on_tray_text_surface(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    set_text_render_mode(state, TB_UI_TEXT_RENDER_SURFACE, true);
}

static void SDLCALL on_tray_text_engine(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    set_text_render_mode(state, TB_UI_TEXT_RENDER_ENGINE, true);
}

static void SDLCALL on_tray_text_debug(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    set_text_debug_logging(state, !state->text_debug_logging, true);
}

static void SDLCALL on_tray_quit(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    state->running = false;
}

static bool setup_tray(AppState *state) {
    state->tray.icon = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA8888);
    if (state->tray.icon == NULL) {
        SDL_Log("SDL_CreateSurface failed: %s", SDL_GetError());
        return false;
    }

    state->tray.tray = SDL_CreateTray(state->tray.icon, "terminal-buddy");
    if (state->tray.tray == NULL) {
        SDL_Log("SDL_CreateTray failed: %s", SDL_GetError());
        return false;
    }

    state->tray.menu = SDL_CreateTrayMenu(state->tray.tray);
    if (state->tray.menu == NULL) {
        SDL_Log("SDL_CreateTrayMenu failed: %s", SDL_GetError());
        return false;
    }

    state->tray.show_hide = SDL_InsertTrayEntryAt(state->tray.menu, -1, "Hide Button", SDL_TRAYENTRY_BUTTON);
    state->tray.mode_parent = SDL_InsertTrayEntryAt(state->tray.menu, -1, "Mode", SDL_TRAYENTRY_SUBMENU);
    state->tray.text_parent = SDL_InsertTrayEntryAt(state->tray.menu, -1, "Text", SDL_TRAYENTRY_SUBMENU);
    state->tray.quit = SDL_InsertTrayEntryAt(state->tray.menu, -1, "Quit", SDL_TRAYENTRY_BUTTON);

    if (state->tray.show_hide == NULL || state->tray.mode_parent == NULL || state->tray.text_parent == NULL || state->tray.quit == NULL) {
        SDL_Log("Failed to create tray menu entries");
        return false;
    }

    state->tray.mode_menu = SDL_CreateTraySubmenu(state->tray.mode_parent);
    if (state->tray.mode_menu == NULL) {
        SDL_Log("SDL_CreateTraySubmenu failed: %s", SDL_GetError());
        return false;
    }
    state->tray.text_menu = SDL_CreateTraySubmenu(state->tray.text_parent);
    if (state->tray.text_menu == NULL) {
        SDL_Log("SDL_CreateTraySubmenu failed: %s", SDL_GetError());
        return false;
    }

    state->tray.mode_standard = SDL_InsertTrayEntryAt(state->tray.mode_menu, -1, "Standard", SDL_TRAYENTRY_CHECKBOX);
    state->tray.mode_coding = SDL_InsertTrayEntryAt(state->tray.mode_menu, -1, "Coding", SDL_TRAYENTRY_CHECKBOX);
    state->tray.mode_terminal = SDL_InsertTrayEntryAt(state->tray.mode_menu, -1, "Terminal", SDL_TRAYENTRY_CHECKBOX);
    state->tray.text_surface = SDL_InsertTrayEntryAt(state->tray.text_menu, -1, "Surface", SDL_TRAYENTRY_CHECKBOX);
    state->tray.text_engine = SDL_InsertTrayEntryAt(state->tray.text_menu, -1, "Engine", SDL_TRAYENTRY_CHECKBOX);
    state->tray.text_debug = SDL_InsertTrayEntryAt(state->tray.text_menu, -1, "Debug Logs", SDL_TRAYENTRY_CHECKBOX);

    if (
        state->tray.mode_standard == NULL
        || state->tray.mode_coding == NULL
        || state->tray.mode_terminal == NULL
        || state->tray.text_surface == NULL
        || state->tray.text_engine == NULL
        || state->tray.text_debug == NULL
    ) {
        SDL_Log("Failed to create tray entries");
        return false;
    }

    SDL_SetTrayEntryCallback(state->tray.show_hide, on_tray_show_hide, state);
    SDL_SetTrayEntryCallback(state->tray.mode_standard, on_tray_mode_standard, state);
    SDL_SetTrayEntryCallback(state->tray.mode_coding, on_tray_mode_coding, state);
    SDL_SetTrayEntryCallback(state->tray.mode_terminal, on_tray_mode_terminal, state);
    SDL_SetTrayEntryCallback(state->tray.text_surface, on_tray_text_surface, state);
    SDL_SetTrayEntryCallback(state->tray.text_engine, on_tray_text_engine, state);
    SDL_SetTrayEntryCallback(state->tray.text_debug, on_tray_text_debug, state);
    SDL_SetTrayEntryCallback(state->tray.quit, on_tray_quit, state);

    refresh_shell_state(state);
    return true;
}

static void destroy_tray(AppState *state) {
    if (state->tray.tray != NULL) {
        SDL_DestroyTray(state->tray.tray);
        state->tray.tray = NULL;
    }

    if (state->tray.icon != NULL) {
        SDL_DestroySurface(state->tray.icon);
        state->tray.icon = NULL;
    }
}

static bool build_wav_buffer(const float *samples, int sample_count, Uint8 **out_data, size_t *out_size) {
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
    *(uint32_t *) (buffer + 24) = AUDIO_SAMPLE_RATE;
    *(uint32_t *) (buffer + 28) = AUDIO_SAMPLE_RATE * sizeof(int16_t);
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

#ifdef _WIN32
static HWND get_app_hwnd(AppState *state) {
    if (state->app_hwnd == NULL && state->window != NULL) {
        SDL_PropertiesID props = SDL_GetWindowProperties(state->window);
        state->app_hwnd = (HWND) SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    }
    return state->app_hwnd;
}

static void update_last_external_window(AppState *state) {
    HWND foreground = GetForegroundWindow();
    HWND app_hwnd = get_app_hwnd(state);

    if (foreground != NULL && foreground != app_hwnd && IsWindow(foreground)) {
        state->last_external_hwnd = foreground;
    }
}

static bool window_process_is_windows_terminal(HWND window) {
    DWORD process_id = 0;
    HANDLE process = NULL;
    wchar_t path[MAX_PATH];
    DWORD path_size = MAX_PATH;
    const wchar_t *basename = NULL;

    GetWindowThreadProcessId(window, &process_id);
    if (process_id == 0) {
        return false;
    }

    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == NULL) {
        return false;
    }

    if (!QueryFullProcessImageNameW(process, 0, path, &path_size)) {
        CloseHandle(process);
        return false;
    }

    CloseHandle(process);

    basename = wcsrchr(path, L'\\');
    basename = (basename != NULL) ? basename + 1 : path;
    return _wcsicmp(basename, L"WindowsTerminal.exe") == 0;
}

static bool focus_window_best_effort(HWND target) {
    HWND foreground = GetForegroundWindow();
    DWORD current_thread = GetCurrentThreadId();
    DWORD target_thread = GetWindowThreadProcessId(target, NULL);
    DWORD foreground_thread = foreground != NULL ? GetWindowThreadProcessId(foreground, NULL) : 0;
    bool attached_to_target = false;
    bool attached_to_foreground = false;
    bool success = false;

    if (target == NULL || !IsWindow(target)) {
        return false;
    }

    if (IsIconic(target)) {
        ShowWindow(target, SW_RESTORE);
    }

    if (target_thread != 0 && target_thread != current_thread) {
        attached_to_target = AttachThreadInput(current_thread, target_thread, TRUE) != 0;
    }
    if (foreground_thread != 0 && foreground_thread != current_thread && foreground_thread != target_thread) {
        attached_to_foreground = AttachThreadInput(current_thread, foreground_thread, TRUE) != 0;
    }

    BringWindowToTop(target);
    SetActiveWindow(target);
    SetFocus(target);
    success = SetForegroundWindow(target) != 0;

    if (attached_to_foreground) {
        AttachThreadInput(current_thread, foreground_thread, FALSE);
    }
    if (attached_to_target) {
        AttachThreadInput(current_thread, target_thread, FALSE);
    }

    return success || GetForegroundWindow() == target;
}

static bool send_paste_shortcut(HWND target) {
    INPUT inputs[6];
    UINT count = 0;
    bool terminal = window_process_is_windows_terminal(target);

    SDL_zeroa(inputs);

    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = VK_CONTROL;
    ++count;

    if (terminal) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        ++count;
    }

    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = 'V';
    ++count;

    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = 'V';
    inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
    ++count;

    if (terminal) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        ++count;
    }

    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = VK_CONTROL;
    inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
    ++count;

    return SendInput(count, inputs, sizeof(INPUT)) == count;
}

static bool try_inject_transcript(AppState *state, const char *transcript) {
    HWND target = state->injection_target_hwnd;

    if (transcript == NULL || transcript[0] == '\0') {
        return false;
    }

    if (target == NULL || !IsWindow(target)) {
        target = state->last_external_hwnd;
    }
    if (target == NULL || !IsWindow(target)) {
        return false;
    }

    if (!SDL_SetClipboardText(transcript)) {
        return false;
    }

    if (!focus_window_best_effort(target)) {
        return false;
    }

    SDL_Delay(80);
    return send_paste_shortcut(target);
}
#else
static void update_last_external_window(AppState *state) {
    (void) state;
}

static bool try_inject_transcript(AppState *state, const char *transcript) {
    (void) state;
    (void) transcript;
    return false;
}
#endif

#ifdef _WIN32
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

static bool send_transcription_request(
    const char *api_key,
    const char *model,
    const char *prompt,
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
    HINTERNET request = NULL;
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
            model,
            boundary,
            boundary,
            prompt,
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
            api_key,
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

    request = WinHttpOpenRequest(connect, L"POST", L"/v1/audio/transcriptions", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request == NULL) {
        *out_error = format_winhttp_error("WinHttpOpenRequest failed");
        goto cleanup;
    }

    if (!WinHttpAddRequestHeaders(request, headers_wide, (DWORD) -1L, WINHTTP_ADDREQ_FLAG_ADD)) {
        *out_error = format_winhttp_error("WinHttpAddRequestHeaders failed");
        goto cleanup;
    }

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, (DWORD) total_len, (DWORD) total_len, 0)) {
        *out_error = format_winhttp_error("WinHttpSendRequest failed");
        goto cleanup;
    }

    if (!WinHttpReceiveResponse(request, NULL)) {
        *out_error = format_winhttp_error("WinHttpReceiveResponse failed");
        goto cleanup;
    }

    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        *out_error = format_winhttp_error("WinHttpQueryHeaders failed");
        goto cleanup;
    }

    if (!read_winhttp_response(request, out_text)) {
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
    if (request != NULL) {
        WinHttpCloseHandle(request);
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
#else
static bool send_transcription_request(
    const char *api_key,
    const char *model,
    const char *prompt,
    const Uint8 *wav_data,
    size_t wav_size,
    char **out_text,
    char **out_error
) {
    (void) api_key;
    (void) model;
    (void) prompt;
    (void) wav_data;
    (void) wav_size;
    (void) out_text;
    *out_error = SDL_strdup("OpenAI transcription is only wired for Windows in this scaffold.");
    return false;
}
#endif

static int transcription_worker(void *userdata) {
    TranscriptionJob *job = (TranscriptionJob *) userdata;
    AppState *state = job->app;
    Uint8 *wav_data = NULL;
    size_t wav_size = 0;
    char *response_text = NULL;
    char *error_text = NULL;
    const char *prompt = mode_prompt(job->mode);

    if (!build_wav_buffer(job->samples, job->sample_count, &wav_data, &wav_size)) {
        error_text = SDL_strdup("Failed to build WAV payload");
    } else if (!send_transcription_request(job->api_key, job->model, prompt, wav_data, wav_size, &response_text, &error_text)) {
        if (error_text == NULL) {
            error_text = SDL_strdup("Transcription request failed");
        }
    }

    SDL_LockMutex(state->transcription.mutex);
    state->transcription.processing = false;
    state->transcription.show_feedback = true;
    state->transcription.worker_done = true;
    if (response_text != NULL) {
        SDL_snprintf(state->transcription.status_text, sizeof(state->transcription.status_text), "%s", "TRANSCRIPT READY");
        SDL_snprintf(state->transcription.transcript_text, sizeof(state->transcription.transcript_text), "%s", response_text);
        state->transcription.clipboard_dirty = true;
    } else {
        SDL_snprintf(state->transcription.status_text, sizeof(state->transcription.status_text), "%s", "TRANSCRIPTION FAILED");
        SDL_snprintf(state->transcription.transcript_text, sizeof(state->transcription.transcript_text), "%s", error_text != NULL ? error_text : "Unknown error");
    }
    SDL_UnlockMutex(state->transcription.mutex);

    SDL_free(response_text);
    SDL_free(error_text);
    SDL_free(wav_data);
    SDL_free(job->samples);
    SDL_free(job->api_key);
    SDL_free(job->model);
    SDL_free(job);
    return 0;
}

static bool begin_transcription(AppState *state) {
    TranscriptionJob *job = NULL;
    float *sample_copy = NULL;

    reload_dev_env(state);
    if (!state->transcription.api_ready) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "OPENAI KEY MISSING",
            "Create .env/dev.env and set OPENAI_API_KEY=... then record again."
        );
        resize_for_mode(state);
        return false;
    }

    if (state->audio.captured_count <= 0) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "NO AUDIO CAPTURED",
            "The mic stream did not deliver any samples."
        );
        resize_for_mode(state);
        return false;
    }

    if (state->transcription.worker != NULL) {
        SDL_LockMutex(state->transcription.mutex);
        if (state->transcription.processing) {
            SDL_UnlockMutex(state->transcription.mutex);
            return false;
        }
        SDL_UnlockMutex(state->transcription.mutex);
        SDL_WaitThread(state->transcription.worker, NULL);
        state->transcription.worker = NULL;
    }

    sample_copy = (float *) SDL_malloc(sizeof(float) * (size_t) state->audio.captured_count);
    if (sample_copy == NULL) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "ALLOC FAILED",
            "Could not copy captured audio for transcription."
        );
        resize_for_mode(state);
        return false;
    }
    SDL_memcpy(sample_copy, state->audio.captured_samples, sizeof(float) * (size_t) state->audio.captured_count);

    job = (TranscriptionJob *) SDL_malloc(sizeof(TranscriptionJob));
    if (job == NULL) {
        SDL_free(sample_copy);
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "ALLOC FAILED",
            "Could not allocate transcription job."
        );
        resize_for_mode(state);
        return false;
    }

    job->app = state;
    job->samples = sample_copy;
    job->sample_count = state->audio.captured_count;
    job->mode = state->mode;
    job->api_key = SDL_strdup(state->transcription.api_key);
    job->model = SDL_strdup(state->transcription.model);

    if (job->api_key == NULL || job->model == NULL) {
        SDL_free(job->api_key);
        SDL_free(job->model);
        SDL_free(job->samples);
        SDL_free(job);
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "ALLOC FAILED",
            "Could not prepare transcription credentials."
        );
        resize_for_mode(state);
        return false;
    }

    SDL_LockMutex(state->transcription.mutex);
    state->transcription.processing = true;
    state->transcription.worker_done = false;
    state->transcription.show_feedback = true;
    state->transcription.clipboard_dirty = false;
    SDL_snprintf(state->transcription.status_text, sizeof(state->transcription.status_text), "%s", "TRANSCRIBING");
    state->transcription.transcript_text[0] = '\0';
    SDL_UnlockMutex(state->transcription.mutex);

    state->transcription.worker = SDL_CreateThread(transcription_worker, "transcription-worker", job);
    if (state->transcription.worker == NULL) {
        SDL_free(job->api_key);
        SDL_free(job->model);
        SDL_free(job->samples);
        SDL_free(job);
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "THREAD FAILED",
            SDL_GetError()
        );
        resize_for_mode(state);
        return false;
    }

    resize_for_mode(state);
    return true;
}

static void handle_panel_tap(AppState *state) {
    bool processing = false;
    bool show_feedback = false;

    if (state->keyboard_visible) {
        hide_keyboard(state);
        return;
    }

    SDL_LockMutex(state->transcription.mutex);
    processing = state->transcription.processing;
    show_feedback = state->transcription.show_feedback;
    SDL_UnlockMutex(state->transcription.mutex);

    if (processing) {
        return;
    }

    if (state->listening) {
        stop_listening(state);
        begin_transcription(state);
        resize_for_mode(state);
        return;
    }

    if (show_feedback) {
        collapse_panel(state);
        return;
    }

    start_listening(state);
}

static void pump_transcription_results(AppState *state) {
    bool should_reap = false;
    bool should_copy = false;
    char transcript[MAX_TRANSCRIPT_TEXT];
    bool injected = false;

    SDL_LockMutex(state->transcription.mutex);
    should_reap = state->transcription.worker_done && state->transcription.worker != NULL;
    should_copy = state->transcription.clipboard_dirty;
    SDL_snprintf(transcript, sizeof(transcript), "%s", state->transcription.transcript_text);
    if (should_copy) {
        state->transcription.clipboard_dirty = false;
    }
    SDL_UnlockMutex(state->transcription.mutex);

    if (should_copy && transcript[0] != '\0') {
        injected = try_inject_transcript(state, transcript);
        SDL_LockMutex(state->transcription.mutex);
        SDL_snprintf(
            state->transcription.status_text,
            sizeof(state->transcription.status_text),
            "%s",
            injected ? "PASTED TO TARGET" : "TRANSCRIPT READY"
        );
        SDL_UnlockMutex(state->transcription.mutex);
        request_redraw(state);
    }

    if (should_reap) {
        SDL_WaitThread(state->transcription.worker, NULL);
        state->transcription.worker = NULL;
        SDL_LockMutex(state->transcription.mutex);
        state->transcription.worker_done = false;
        SDL_UnlockMutex(state->transcription.mutex);
        request_redraw(state);
    }
}

static bool ui_is_processing(AppState *state) {
    bool processing = false;

    SDL_LockMutex(state->transcription.mutex);
    processing = state->transcription.processing;
    SDL_UnlockMutex(state->transcription.mutex);
    return processing;
}

static void destroy_transcription_state(AppState *state) {
    if (state->transcription.worker != NULL) {
        SDL_WaitThread(state->transcription.worker, NULL);
        state->transcription.worker = NULL;
    }

    if (state->transcription.mutex != NULL) {
        SDL_DestroyMutex(state->transcription.mutex);
        state->transcription.mutex = NULL;
    }

    SDL_free(state->transcription.env_path);
    state->transcription.env_path = NULL;
}

int main(void) {
    AppState state;

    SDL_zero(state);
    state.running = true;
    state.visible = true;
    state.needs_redraw = true;
    state.transcription.mutex = SDL_CreateMutex();
    clear_keyboard_press(&state);
    tb_keyboard_mod_init(&state.keyboard_mods);

    if (state.transcription.mutex == NULL) {
        return fail("SDL_CreateMutex failed");
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        SDL_DestroyMutex(state.transcription.mutex);
        return fail("SDL_Init failed");
    }

    g_log_mutex = SDL_CreateMutex();
    SDL_GetLogOutputFunction(&g_default_log_output, &g_default_log_userdata);
    SDL_SetLogOutputFunction(terminal_buddy_log_output, NULL);

    load_preferences(&state);
    reload_dev_env(&state);

    state.window = SDL_CreateWindow(
        "terminal-buddy",
        IDLE_WINDOW_SIZE,
        IDLE_WINDOW_SIZE,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (state.window == NULL) {
        destroy_transcription_state(&state);
        SDL_Quit();
        return fail("SDL_CreateWindow failed");
    }

    state.renderer = SDL_CreateRenderer(state.window, "direct3d11");
    if (state.renderer == NULL) {
        SDL_DestroyWindow(state.window);
        destroy_transcription_state(&state);
        SDL_Quit();
        return fail("SDL_CreateRenderer failed");
    }

    if (!tb_ui_init(state.renderer)) {
        tb_ui_shutdown();
        SDL_DestroyRenderer(state.renderer);
        SDL_DestroyWindow(state.window);
        destroy_transcription_state(&state);
        SDL_Quit();
        return fail("tb_ui_init failed");
    }

    SDL_SetWindowPosition(state.window, state.window_x, state.window_y);
    SDL_SetWindowFocusable(state.window, false);
    sync_window_metrics(&state);
    resize_for_mode(&state);
#ifdef _WIN32
    state.app_hwnd = get_app_hwnd(&state);
#endif

    tb_ui_set_text_render_mode(state.text_render_mode);
    tb_ui_set_text_debug_logging(state.text_debug_logging);
    set_log_capture_enabled(state.text_debug_logging);

    initialize_audio(&state);

    if (!setup_tray(&state)) {
        SDL_Log("Continuing without tray integration");
    }

    while (state.running) {
        bool processing = false;
        bool animated = false;
        Uint64 now_ms = 0;
        Uint64 frame_interval_ms = 33;
        Uint32 sleep_ms = 50;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    state.running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) {
                        state.running = false;
                    }
                    break;
                case SDL_EVENT_WINDOW_EXPOSED:
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    sync_window_metrics(&state);
                    request_redraw(&state);
                    break;
                case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                    sync_window_metrics(&state);
                    resize_for_mode(&state);
                    request_redraw(&state);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    handle_mouse_down(&state, &event.button);
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    handle_mouse_motion(&state);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    handle_mouse_up(&state, &event.button);
                    break;
                case SDL_EVENT_FINGER_DOWN:
                    handle_finger_down(&state, &event.tfinger);
                    break;
                case SDL_EVENT_FINGER_MOTION:
                    handle_finger_motion(&state, &event.tfinger);
                    break;
                case SDL_EVENT_FINGER_UP:
                case SDL_EVENT_FINGER_CANCELED:
                    handle_finger_up(&state, &event.tfinger);
                    break;
                default:
                    break;
            }
        }

        update_last_external_window(&state);
        update_audio_meter(&state);
        pump_bubble_long_press(&state);
        pump_keyboard_repeat(&state);
        pump_transcription_results(&state);

        processing = ui_is_processing(&state);
        animated = state.listening || processing || state.drag.pressed;
        now_ms = SDL_GetTicks();

        if (state.visible) {
            if (animated && (now_ms - state.last_render_ms >= frame_interval_ms)) {
                request_redraw(&state);
            }

            if (state.needs_redraw) {
                render(&state);
            }
        } else {
            sleep_ms = 50;
        }

        if (state.visible) {
            if (state.keyboard_press.pressed) {
                sleep_ms = 8;
            } else if (state.drag.pressed) {
                sleep_ms = 8;
            } else if (animated) {
                Uint64 elapsed_ms = now_ms - state.last_render_ms;
                if (elapsed_ms >= frame_interval_ms) {
                    sleep_ms = 1;
                } else {
                    Uint64 remaining_ms = frame_interval_ms - elapsed_ms;
                    sleep_ms = (Uint32) SDL_min(remaining_ms, 16);
                }
            } else {
                sleep_ms = state.needs_redraw ? 1 : 50;
            }
        }

        SDL_Delay(sleep_ms);
    }

    if (state.listening) {
        stop_listening(&state);
    }

    save_preferences(&state);
    destroy_tray(&state);
    destroy_audio(&state);
    destroy_transcription_state(&state);
    tb_ui_shutdown();

    if (state.prefs_path != NULL) {
        SDL_free(state.prefs_path);
    }

    SDL_DestroyRenderer(state.renderer);
    SDL_DestroyWindow(state.window);
    shutdown_log_capture();
    SDL_Quit();
    return 0;
}
