#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>

#include "transcription_backend.h"
#include "owned_terminal_window.h"
#include "sidecar_client.h"
#include "ui_clay.h"

#ifndef TERMINAL_BUDDY_SOURCE_DIR
#define TERMINAL_BUDDY_SOURCE_DIR "."
#endif

#define IDLE_WINDOW_WIDTH 164
#define IDLE_WINDOW_HEIGHT 96
#define EXPANDED_WINDOW_WIDTH 424
#define EXPANDED_WINDOW_HEIGHT 132
#define DRAG_THRESHOLD 10.0f
#define AUDIO_HISTORY_COUNT 20
#define AUDIO_SAMPLE_RATE 24000
#define AUDIO_LEVEL_BOOST 3.5f
#define AUDIO_LEVEL_DECAY 0.88f
#define MAX_STATUS_TEXT 256
#define MAX_TRANSCRIPT_TEXT 2048
#define MAX_METRICS_TEXT 768
#define TB_OWNED_TERMINAL_PROJECT_ID "buddy-terminal"
#define TB_OWNED_TERMINAL_SHELL_TARGET_ID "shell:buddy-terminal"

#ifdef _WIN32
#define TB_GLOBAL_HOTKEY_ID 0x5442
#define TB_GLOBAL_HOTKEY_MODIFIERS (MOD_CONTROL | MOD_ALT | MOD_NOREPEAT)
#define TB_GLOBAL_HOTKEY_VKEY VK_SPACE
#endif

typedef enum PointerKind {
    POINTER_NONE = 0,
    POINTER_MOUSE,
    POINTER_FINGER
} PointerKind;

typedef enum TapTarget {
    TAP_TARGET_NONE = 0,
    TAP_TARGET_PANEL,
    TAP_TARGET_TERMINAL
} TapTarget;

typedef enum AppMode {
    APP_MODE_STANDARD = 0,
    APP_MODE_TERMINAL
} AppMode;

typedef enum ControlMode {
    CONTROL_MODE_WIDGET = 0,
    CONTROL_MODE_HOTKEY
} ControlMode;

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
    TapTarget tap_target;
    float start_global_x;
    float start_global_y;
    int window_start_x;
    int window_start_y;
} DragState;

typedef struct DragPerfTrace {
    bool active;
    Uint64 started_counter;
    Uint64 render_total_counter;
    Uint64 render_max_counter;
    uint32_t pointer_update_count;
    uint32_t window_move_count;
    uint32_t redraw_request_count;
    uint32_t render_count;
    uint32_t window_exposed_count;
} DragPerfTrace;

typedef struct TrayState {
    SDL_Tray *tray;
    SDL_TrayMenu *menu;
    SDL_TrayMenu *control_menu;
    SDL_TrayMenu *backend_menu;
    SDL_TrayMenu *npu_model_menu;
    SDL_TrayMenu *mode_menu;
    SDL_TrayMenu *text_menu;
    SDL_TrayEntry *show_hide;
    SDL_TrayEntry *control_parent;
    SDL_TrayEntry *control_widget;
    SDL_TrayEntry *control_hotkey;
    SDL_TrayEntry *backend_parent;
    SDL_TrayEntry *backend_openai;
    SDL_TrayEntry *backend_npu;
    SDL_TrayEntry *npu_model_parent;
    SDL_TrayEntry *npu_model_tiny;
    SDL_TrayEntry *npu_model_base;
    SDL_TrayEntry *npu_model_small;
    SDL_TrayEntry *npu_model_turbo;
    SDL_TrayEntry *mode_parent;
    SDL_TrayEntry *mode_standard;
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
    bool terminal_submit_ready;
    char *env_path;
    TbTranscriptionConfig config;
    char status_text[MAX_STATUS_TEXT];
    char transcript_text[MAX_TRANSCRIPT_TEXT];
    char metrics_text[MAX_METRICS_TEXT];
    char sidecar_detail_text[TB_SIDECAR_TEXT_MAX];
    bool pending_agent_launch;
    char pending_agent_provider[TB_SIDECAR_CATEGORY_MAX];
    char pending_agent_project_id[TB_SIDECAR_ID_MAX];
    char pending_agent_shell_target_id[TB_SIDECAR_ID_MAX];
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
    bool widget_visible;
    bool needs_redraw;
    bool perf_logging_enabled;
    bool text_debug_logging;
    bool hotkey_registered;
    int window_x;
    int window_y;
    AppMode mode;
    ControlMode control_mode;
    TbUiTextRenderMode text_render_mode;
    Uint64 last_render_ms;
    Uint64 perf_counter_freq;
    Uint32 hotkey_event_type;
    char *prefs_path;
    SDL_Window *window;
    SDL_Renderer *renderer;
    DragState drag;
    DragPerfTrace drag_perf;
    WindowMetrics window_metrics;
    TrayState tray;
    AudioState audio;
    TranscriptionState transcription;
    TbSidecarClient sidecar;
    TbOwnedTerminalWindow *owned_terminal_window;
#ifdef _WIN32
    HWND app_hwnd;
    HWND last_external_hwnd;
    HWND injection_target_hwnd;
    char owned_terminal_agent_target_id[TB_SIDECAR_ID_MAX];
    char owned_terminal_agent_provider[TB_SIDECAR_CATEGORY_MAX];
    HWND terminal_companion_hwnd;
    HANDLE terminal_companion_process;
    DWORD terminal_companion_pid;
#endif
} AppState;

typedef struct TranscriptionJob {
    AppState *app;
    float *samples;
    int sample_count;
    AppMode mode;
    TbTranscriptionConfig config;
} TranscriptionJob;

static SDL_LogOutputFunction g_default_log_output;
static void *g_default_log_userdata = NULL;
static SDL_Mutex *g_log_mutex = NULL;
static FILE *g_log_file = NULL;
static bool g_log_capture_enabled = false;
static char *g_log_path = NULL;
static FILE *g_perf_log_file = NULL;
static char *g_perf_log_path = NULL;

static void perf_log_message(const AppState *state, const char *fmt, ...);
static void handle_panel_tap(AppState *state);
static bool update_hotkey_registration(AppState *state);
static void apply_window_visibility_policy(AppState *state);
static bool try_submit_terminal_target(AppState *state);
static void clamp_window_to_display(AppState *state);
static bool ui_is_processing(AppState *state);
static void refresh_shell_state(AppState *state);
static void refresh_sidecar_feedback(AppState *state);
static void clear_pending_agent_launch_locked(TranscriptionState *transcription);
static bool register_pending_agent_launch(AppState *state);
static void populate_sidecar_context(
    AppState *state,
    char *project_id_out,
    size_t project_id_size,
    char *target_id_out,
    size_t target_id_size
);
#ifdef _WIN32
static bool launch_or_focus_terminal_companion(AppState *state);
static bool focus_window_best_effort(HWND target);
static bool upsert_owned_terminal_shell_target(AppState *state, const char *status);
#endif

static int fail(const char *message) {
    SDL_Log("%s: %s", message, SDL_GetError());
    return 1;
}

static float squaref(float value) {
    return value * value;
}

static const char *mode_key(AppMode mode) {
    switch (mode) {
        case APP_MODE_STANDARD:
            return "standard";
        case APP_MODE_TERMINAL:
            return "terminal";
        default:
            return "standard";
    }
}

static const char *control_mode_key(ControlMode mode) {
    switch (mode) {
        case CONTROL_MODE_HOTKEY:
            return "hotkey";
        case CONTROL_MODE_WIDGET:
        default:
            return "widget";
    }
}

static const char *mode_label(AppMode mode) {
    switch (mode) {
        case APP_MODE_STANDARD:
            return "Standard";
        case APP_MODE_TERMINAL:
            return "Terminal";
        default:
            return "Standard";
    }
}

static const char *control_mode_label(ControlMode mode) {
    switch (mode) {
        case CONTROL_MODE_HOTKEY:
            return "Hotkey";
        case CONTROL_MODE_WIDGET:
        default:
            return "Widget";
    }
}

static const char *transcription_backend_env_value(TbTranscriptionBackendKind backend) {
    switch (backend) {
        case TB_TRANSCRIPTION_BACKEND_NPU:
            return "npu";
        case TB_TRANSCRIPTION_BACKEND_OPENAI:
        default:
            return "openai";
    }
}

static const char *transcription_backend_label(TbTranscriptionBackendKind backend) {
    switch (backend) {
        case TB_TRANSCRIPTION_BACKEND_NPU:
            return "NPU";
        case TB_TRANSCRIPTION_BACKEND_OPENAI:
        default:
            return "OpenAI";
    }
}

static const char *selected_npu_model_id(const TbTranscriptionConfig *config) {
    if (config != NULL) {
        if (config->npu_model[0] != '\0') {
            return config->npu_model;
        }
        if (config->backend == TB_TRANSCRIPTION_BACKEND_NPU && config->model[0] != '\0') {
            return config->model;
        }
    }

    return "whisper_base_en";
}

static const char *npu_model_label(const char *model_id) {
    if (model_id != NULL) {
        if (SDL_strcmp(model_id, "whisper_tiny_en") == 0) {
            return "Tiny";
        }
        if (SDL_strcmp(model_id, "whisper_small_en") == 0) {
            return "Small";
        }
        if (SDL_strcmp(model_id, "whisper_large_v3_turbo") == 0) {
            return "Large V3 Turbo";
        }
    }

    return "Base";
}

static const char *mode_prompt(AppMode mode) {
    switch (mode) {
        case APP_MODE_STANDARD:
            return "This is spoken dictation. Remove filler words and false starts, add light punctuation, and preserve intent.";
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

static ControlMode parse_control_mode_key(const char *value) {
    if (value != NULL && SDL_strcmp(value, "hotkey") == 0) {
        return CONTROL_MODE_HOTKEY;
    }
    return CONTROL_MODE_WIDGET;
}

static TbUiTextRenderMode parse_text_render_mode_key(const char *value) {
    if (value != NULL && SDL_strcmp(value, "engine") == 0) {
        return TB_UI_TEXT_RENDER_ENGINE;
    }
    return TB_UI_TEXT_RENDER_SURFACE;
}

static const char *hotkey_label(void) {
#ifdef _WIN32
    return "Ctrl+Alt+Space";
#else
    return "Unavailable";
#endif
}

static AppMode parse_mode_key(const char *value) {
    if (SDL_strcmp(value, "terminal") == 0) {
        return APP_MODE_TERMINAL;
    }
    return APP_MODE_STANDARD;
}

static ModePalette get_mode_palette(AppMode mode) {
    switch (mode) {
        case APP_MODE_STANDARD:
            return (ModePalette) {42, 212, 138, 64, 79, 255};
        case APP_MODE_TERMINAL:
            return (ModePalette) {255, 171, 64, 255, 111, 76};
        default:
            return (ModePalette) {42, 212, 138, 64, 79, 255};
    }
}

static bool is_panel_expanded(const AppState *state) {
    return state->listening || state->transcription.processing || state->transcription.show_feedback;
}

static bool desired_window_visibility(const AppState *state) {
    if (state == NULL) {
        return false;
    }

    if (state->control_mode == CONTROL_MODE_HOTKEY) {
        return is_panel_expanded(state);
    }

    return state->widget_visible;
}

static float current_touch_scale(const AppState *state) {
    if (state != NULL && state->window_metrics.touch_scale > 0.01f) {
        return state->window_metrics.touch_scale;
    }
    return 1.0f;
}

static int target_window_width(const AppState *state) {
    float touch_scale = current_touch_scale(state);

    if (is_panel_expanded(state)) {
        return (int) SDL_roundf((float) EXPANDED_WINDOW_WIDTH * touch_scale);
    }
    return (int) SDL_roundf((float) IDLE_WINDOW_WIDTH * touch_scale);
}

static int target_window_height(const AppState *state) {
    float touch_scale = current_touch_scale(state);

    if (is_panel_expanded(state)) {
        return (int) SDL_roundf((float) EXPANDED_WINDOW_HEIGHT * touch_scale);
    }
    return (int) SDL_roundf((float) IDLE_WINDOW_HEIGHT * touch_scale);
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
        if (state->drag_perf.active) {
            state->drag_perf.redraw_request_count += 1;
        }
        state->needs_redraw = true;
    }
}

static double perf_counters_to_ms(const AppState *state, Uint64 counters) {
    Uint64 frequency = 0;

    if (state == NULL) {
        return 0.0;
    }

    frequency = state->perf_counter_freq > 0 ? state->perf_counter_freq : SDL_GetPerformanceFrequency();
    if (frequency == 0) {
        return 0.0;
    }

    return ((double) counters * 1000.0) / (double) frequency;
}

static void drag_perf_trace_begin(AppState *state) {
    if (state == NULL || !state->perf_logging_enabled || state->drag_perf.active) {
        return;
    }

    SDL_zero(state->drag_perf);
    state->drag_perf.active = true;
    state->drag_perf.started_counter = SDL_GetPerformanceCounter();
}

static void drag_perf_trace_end(AppState *state) {
    double duration_ms = 0.0;
    double avg_render_ms = 0.0;
    double max_render_ms = 0.0;

    if (state == NULL || !state->drag_perf.active) {
        return;
    }

    duration_ms = perf_counters_to_ms(state, SDL_GetPerformanceCounter() - state->drag_perf.started_counter);
    if (state->drag_perf.render_count > 0) {
        avg_render_ms = perf_counters_to_ms(state, state->drag_perf.render_total_counter) / (double) state->drag_perf.render_count;
        max_render_ms = perf_counters_to_ms(state, state->drag_perf.render_max_counter);
    }

    perf_log_message(
        state,
        "drag summary surface=%s duration=%.1fms pointer_updates=%u set_window_pos=%u exposes=%u redraw_requests=%u renders=%u avg_render=%.2fms max_render=%.2fms",
        "widget",
        duration_ms,
        state->drag_perf.pointer_update_count,
        state->drag_perf.window_move_count,
        state->drag_perf.window_exposed_count,
        state->drag_perf.redraw_request_count,
        state->drag_perf.render_count,
        avg_render_ms,
        max_render_ms
    );

    SDL_zero(state->drag_perf);
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

static char *build_perf_log_path(void) {
    char *path = NULL;

    if (SDL_CreateDirectory(TERMINAL_BUDDY_SOURCE_DIR "/logs")) {
        if (SDL_asprintf(&path, "%s/logs/ui-perf.log", TERMINAL_BUDDY_SOURCE_DIR) < 0) {
            return NULL;
        }
    }

    return path;
}

static bool env_value_is_truthy(const char *value) {
    return value != NULL
        && (SDL_strcmp(value, "1") == 0 || SDL_strcasecmp(value, "true") == 0 || SDL_strcasecmp(value, "yes") == 0);
}

static bool perf_logging_from_env(void) {
    return env_value_is_truthy(SDL_getenv("TB_UI_PERF"));
}

static void perf_log_message(const AppState *state, const char *fmt, ...) {
    char message[512];
    va_list args;

    if (state == NULL || !state->perf_logging_enabled || fmt == NULL) {
        return;
    }

    va_start(args, fmt);
    SDL_vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    SDL_Log("%s", message);

    if (g_perf_log_file == NULL) {
        return;
    }

    if (g_log_mutex != NULL) {
        SDL_LockMutex(g_log_mutex);
    }

    fprintf(g_perf_log_file, "%s\n", message);
    fflush(g_perf_log_file);

    if (g_log_mutex != NULL) {
        SDL_UnlockMutex(g_log_mutex);
    }
}

static void setup_perf_logging(AppState *state) {
    if (state == NULL || !state->perf_logging_enabled) {
        return;
    }

    if (g_perf_log_path == NULL) {
        g_perf_log_path = build_perf_log_path();
    }
    if (g_perf_log_file == NULL && g_perf_log_path != NULL) {
        fopen_s(&g_perf_log_file, g_perf_log_path, "a");
    }
    if (g_perf_log_file != NULL) {
        perf_log_message(state, "---- ui perf session ----");
        perf_log_message(state, "ui perf log path: %s", g_perf_log_path);
    }
}

static void shutdown_perf_logging(void) {
    if (g_perf_log_file != NULL) {
        fclose(g_perf_log_file);
        g_perf_log_file = NULL;
    }

    SDL_free(g_perf_log_path);
    g_perf_log_path = NULL;
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

static void clear_pending_agent_launch_locked(TranscriptionState *transcription) {
    if (transcription == NULL) {
        return;
    }

    transcription->pending_agent_launch = false;
    transcription->pending_agent_provider[0] = '\0';
    transcription->pending_agent_project_id[0] = '\0';
    transcription->pending_agent_shell_target_id[0] = '\0';
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
    state->transcription.terminal_submit_ready = false;
    clear_pending_agent_launch_locked(&state->transcription);
    if (clipboard_dirty) {
        state->transcription.clipboard_dirty = true;
    }

    if (status != NULL) {
        SDL_snprintf(state->transcription.status_text, sizeof(state->transcription.status_text), "%s", status);
    }
    if (transcript != NULL) {
        SDL_snprintf(state->transcription.transcript_text, sizeof(state->transcription.transcript_text), "%s", transcript);
    }
    state->transcription.sidecar_detail_text[0] = '\0';
    SDL_UnlockMutex(state->transcription.mutex);
}

static void format_transcription_metrics_text(
    const TbTranscriptionConfig *config,
    const TbTranscriptionNpuTimingStats *stats,
    char *out_text,
    size_t out_text_size
) {
    const char *backend_label = NULL;
    const char *runtime_state = NULL;

    if (out_text == NULL || out_text_size == 0) {
        return;
    }

    out_text[0] = '\0';
    if (config == NULL || stats == NULL || !stats->valid) {
        return;
    }

    backend_label = config->backend_name[0] != '\0' ? config->backend_name : "backend";
    runtime_state = stats->runtime_cache_hit ? "warm" : "cold";

    SDL_snprintf(
        out_text,
        out_text_size,
        "%s %s | total %.2fs | mel %.0fms | enc %.0fms | dec %.0fms\n%d steps | %d tokens | %d chunk%s | init %.0fms",
        backend_label,
        runtime_state,
        stats->total_ms / 1000.0,
        stats->feature_ms,
        stats->encoder_ms,
        stats->decoder_ms,
        stats->decoder_steps,
        stats->emitted_token_count,
        stats->chunk_count,
        stats->chunk_count == 1 ? "" : "s",
        stats->session_init_ms
    );
}

static void transcription_clear_feedback(AppState *state) {
    SDL_LockMutex(state->transcription.mutex);
    state->transcription.show_feedback = false;
    state->transcription.terminal_submit_ready = false;
    clear_pending_agent_launch_locked(&state->transcription);
    state->transcription.status_text[0] = '\0';
    state->transcription.transcript_text[0] = '\0';
    state->transcription.metrics_text[0] = '\0';
    state->transcription.sidecar_detail_text[0] = '\0';
    SDL_UnlockMutex(state->transcription.mutex);
}

static void copy_transcription_snapshot(
    AppState *state,
    bool *processing,
    bool *show_feedback,
    bool *terminal_submit_ready,
    char *status_out,
    size_t status_size,
    char *metrics_out,
    size_t metrics_size,
    char *transcript_out,
    size_t transcript_size
) {
    char combined_metrics[MAX_METRICS_TEXT];

    SDL_LockMutex(state->transcription.mutex);
    *processing = state->transcription.processing;
    *show_feedback = state->transcription.show_feedback;
    *terminal_submit_ready = state->transcription.terminal_submit_ready;
    SDL_snprintf(status_out, status_size, "%s", state->transcription.status_text);
    SDL_snprintf(transcript_out, transcript_size, "%s", state->transcription.transcript_text);
    combined_metrics[0] = '\0';
    if (state->transcription.metrics_text[0] != '\0' && state->transcription.sidecar_detail_text[0] != '\0') {
        SDL_snprintf(
            combined_metrics,
            sizeof(combined_metrics),
            "%s\n%s",
            state->transcription.metrics_text,
            state->transcription.sidecar_detail_text
        );
    } else if (state->transcription.metrics_text[0] != '\0') {
        SDL_snprintf(combined_metrics, sizeof(combined_metrics), "%s", state->transcription.metrics_text);
    } else if (state->transcription.sidecar_detail_text[0] != '\0') {
        SDL_snprintf(combined_metrics, sizeof(combined_metrics), "%s", state->transcription.sidecar_detail_text);
    }
    SDL_snprintf(metrics_out, metrics_size, "%s", combined_metrics);
    SDL_UnlockMutex(state->transcription.mutex);
}

static void refresh_sidecar_feedback(AppState *state) {
    bool changed = false;
    char detail[TB_SIDECAR_TEXT_MAX];
    bool show_feedback = false;
    bool has_transcript = false;

    if (state == NULL) {
        return;
    }

    tb_sidecar_client_copy_snapshot(&state->sidecar, NULL, detail, sizeof(detail));

    SDL_LockMutex(state->transcription.mutex);
    show_feedback = state->transcription.show_feedback;
    has_transcript = state->transcription.transcript_text[0] != '\0';
    if ((!show_feedback || !has_transcript) && state->transcription.sidecar_detail_text[0] != '\0') {
        state->transcription.sidecar_detail_text[0] = '\0';
        changed = true;
    } else if (show_feedback && has_transcript && SDL_strcmp(state->transcription.sidecar_detail_text, detail) != 0) {
        SDL_snprintf(
            state->transcription.sidecar_detail_text,
            sizeof(state->transcription.sidecar_detail_text),
            "%s",
            detail
        );
        changed = true;
    }
    SDL_UnlockMutex(state->transcription.mutex);

    if (changed) {
        request_redraw(state);
    }
}

static void reload_transcription_config(AppState *state) {
    if (state->transcription.env_path == NULL) {
        state->transcription.env_path = build_env_path();
    }
    if (state->transcription.env_path == NULL) {
        return;
    }
    tb_transcription_reload_env(&state->transcription.config, state->transcription.env_path);
}

static bool append_text(char **buffer, size_t *length, const char *text) {
    size_t text_length = 0;
    char *grown = NULL;

    if (buffer == NULL || length == NULL || text == NULL) {
        return false;
    }

    text_length = SDL_strlen(text);
    grown = (char *) SDL_realloc(*buffer, *length + text_length + 1);
    if (grown == NULL) {
        return false;
    }

    SDL_memcpy(grown + *length, text, text_length);
    *length += text_length;
    grown[*length] = '\0';
    *buffer = grown;
    return true;
}

static bool write_env_assignment(const char *env_path, const char *key, const char *value, char **out_error) {
    size_t size = 0;
    void *raw = NULL;
    char *contents = NULL;
    char *rebuilt = NULL;
    char *context = NULL;
    char *line = NULL;
    size_t rebuilt_length = 0;
    bool found = false;
    bool ok = false;

    if (out_error != NULL) {
        *out_error = NULL;
    }

    if (env_path == NULL || key == NULL || key[0] == '\0' || value == NULL) {
        if (out_error != NULL) {
            *out_error = SDL_strdup("Invalid env update request.");
        }
        return false;
    }

    raw = SDL_LoadFile(env_path, &size);
    if (raw != NULL) {
        contents = (char *) SDL_malloc(size + 1);
        if (contents == NULL) {
            if (out_error != NULL) {
                *out_error = SDL_strdup("Could not allocate env buffer.");
            }
            SDL_free(raw);
            return false;
        }

        SDL_memcpy(contents, raw, size);
        contents[size] = '\0';
        SDL_free(raw);
        raw = NULL;

        line = strtok_s(contents, "\r\n", &context);
        while (line != NULL) {
            if (SDL_strncmp(line, key, SDL_strlen(key)) == 0 && line[SDL_strlen(key)] == '=') {
                char *replacement = NULL;

                found = true;
                if (SDL_asprintf(&replacement, "%s=%s\n", key, value) < 0) {
                    replacement = NULL;
                }
                if (replacement == NULL || !append_text(&rebuilt, &rebuilt_length, replacement)) {
                    SDL_free(replacement);
                    if (out_error != NULL) {
                        *out_error = SDL_strdup("Could not rebuild env file.");
                    }
                    goto cleanup;
                }
                SDL_free(replacement);
            } else {
                if (!append_text(&rebuilt, &rebuilt_length, line) || !append_text(&rebuilt, &rebuilt_length, "\n")) {
                    if (out_error != NULL) {
                        *out_error = SDL_strdup("Could not rebuild env file.");
                    }
                    goto cleanup;
                }
            }

            line = strtok_s(NULL, "\r\n", &context);
        }
    }

    if (!found) {
        if (!append_text(&rebuilt, &rebuilt_length, key)
            || !append_text(&rebuilt, &rebuilt_length, "=")
            || !append_text(&rebuilt, &rebuilt_length, value)
            || !append_text(&rebuilt, &rebuilt_length, "\n")) {
            if (out_error != NULL) {
                *out_error = SDL_strdup("Could not append env setting.");
            }
            goto cleanup;
        }
    }

    SDL_CreateDirectory(TERMINAL_BUDDY_SOURCE_DIR "/.env");
    if (!SDL_SaveFile(env_path, rebuilt != NULL ? rebuilt : "", rebuilt_length)) {
        if (out_error != NULL) {
            SDL_asprintf(out_error, "SDL_SaveFile failed while updating %s: %s", env_path, SDL_GetError());
        }
        goto cleanup;
    }

    ok = true;

cleanup:
    SDL_free(contents);
    SDL_free(rebuilt);
    return ok;
}

static bool switch_transcription_backend(AppState *state, TbTranscriptionBackendKind backend) {
    TbTranscriptionBackendKind previous_backend = TB_TRANSCRIPTION_BACKEND_OPENAI;
    char detail[MAX_TRANSCRIPT_TEXT];
    char *error_text = NULL;

    if (state == NULL) {
        return false;
    }

    if (state->listening || ui_is_processing(state)) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "BACKEND LOCKED",
            "Stop recording before switching transcription backends."
        );
        request_redraw(state);
        return false;
    }

    if (state->transcription.env_path == NULL) {
        state->transcription.env_path = build_env_path();
    }
    if (state->transcription.env_path == NULL) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "BACKEND SWITCH FAILED",
            "Could not resolve the local transcription env file."
        );
        request_redraw(state);
        return false;
    }

    previous_backend = state->transcription.config.backend;
    if (previous_backend == backend) {
        refresh_shell_state(state);
        return true;
    }

    if (!write_env_assignment(
            state->transcription.env_path,
            "TB_TRANSCRIPTION_BACKEND",
            transcription_backend_env_value(backend),
            &error_text
        )) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "BACKEND SWITCH FAILED",
            error_text != NULL ? error_text : "Could not update the transcription backend setting."
        );
        SDL_free(error_text);
        request_redraw(state);
        return false;
    }

    if (previous_backend == TB_TRANSCRIPTION_BACKEND_NPU && backend != TB_TRANSCRIPTION_BACKEND_NPU) {
        tb_transcription_backend_npu_shutdown();
    }

    reload_transcription_config(state);
    refresh_shell_state(state);

    if (state->transcription.config.ready) {
        SDL_snprintf(
            detail,
            sizeof(detail),
            "Future recordings will use %s.",
            transcription_backend_label(state->transcription.config.backend)
        );
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "BACKEND CHANGED",
            detail
        );
    } else {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            state->transcription.config.missing_status,
            state->transcription.config.missing_detail
        );
    }

    request_redraw(state);
    return true;
}

static bool switch_npu_model(AppState *state, const char *model_id) {
    char detail[MAX_TRANSCRIPT_TEXT];
    char *error_text = NULL;

    if (state == NULL || model_id == NULL || model_id[0] == '\0') {
        return false;
    }

    if (state->listening || ui_is_processing(state)) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "MODEL LOCKED",
            "Stop recording before switching NPU models."
        );
        request_redraw(state);
        return false;
    }

    if (state->transcription.env_path == NULL) {
        state->transcription.env_path = build_env_path();
    }
    if (state->transcription.env_path == NULL) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "MODEL SWITCH FAILED",
            "Could not resolve the local transcription env file."
        );
        request_redraw(state);
        return false;
    }

    if (SDL_strcmp(selected_npu_model_id(&state->transcription.config), model_id) == 0) {
        refresh_shell_state(state);
        return true;
    }

    if (!write_env_assignment(
            state->transcription.env_path,
            "TB_TRANSCRIPTION_MODEL",
            model_id,
            &error_text
        )) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "MODEL SWITCH FAILED",
            error_text != NULL ? error_text : "Could not update the NPU model setting."
        );
        SDL_free(error_text);
        request_redraw(state);
        return false;
    }

    if (state->transcription.config.backend == TB_TRANSCRIPTION_BACKEND_NPU) {
        tb_transcription_backend_npu_shutdown();
    }

    reload_transcription_config(state);
    refresh_shell_state(state);

    if (state->transcription.config.backend == TB_TRANSCRIPTION_BACKEND_NPU) {
        if (state->transcription.config.ready) {
            SDL_snprintf(
                detail,
                sizeof(detail),
                "Future recordings will use the NPU %s model.",
                npu_model_label(selected_npu_model_id(&state->transcription.config))
            );
            transcription_set_ui(
                state,
                false,
                true,
                false,
                "MODEL CHANGED",
                detail
            );
        } else {
            transcription_set_ui(
                state,
                false,
                true,
                false,
                state->transcription.config.missing_status,
                state->transcription.config.missing_detail
            );
        }
    } else {
        SDL_snprintf(
            detail,
            sizeof(detail),
            "Saved the NPU %s model. Switch backend to NPU to use it.",
            npu_model_label(selected_npu_model_id(&state->transcription.config))
        );
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "MODEL SAVED",
            detail
        );
    }

    request_redraw(state);
    return true;
}

static void save_preferences(const AppState *state) {
    char *contents = NULL;

    if (state->prefs_path == NULL) {
        return;
    }

    if (SDL_asprintf(
            &contents,
            "window_x=%d\nwindow_y=%d\nmode=%s\ncontrol_mode=%s\ntext_mode=%s\ntext_debug=%d\n",
            state->window_x,
            state->window_y,
            mode_key(state->mode),
            control_mode_key(state->control_mode),
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
    state->control_mode = CONTROL_MODE_WIDGET;
    state->widget_visible = true;
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
                } else if (SDL_strncmp(line, "control_mode=", 13) == 0) {
                    state->control_mode = parse_control_mode_key(line + 13);
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
    clamp_window_to_display(state);
    return true;
}

static void sync_window_position_from_os(AppState *state) {
    int window_x = 0;
    int window_y = 0;

    if (state == NULL || state->window == NULL) {
        return;
    }

    if (SDL_GetWindowPosition(state->window, &window_x, &window_y)) {
        state->window_x = window_x;
        state->window_y = window_y;
    }
}

static void clamp_window_to_display(AppState *state) {
    SDL_DisplayID display_id = 0;
    SDL_Rect usable_bounds;
    int width = 0;
    int height = 0;
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    int clamped_x = 0;
    int clamped_y = 0;

    if (state == NULL || state->window == NULL) {
        return;
    }

    sync_window_position_from_os(state);
    display_id = SDL_GetDisplayForWindow(state->window);
    if (display_id == 0 || !SDL_GetDisplayUsableBounds(display_id, &usable_bounds)) {
        return;
    }

    width = target_window_width(state);
    height = target_window_height(state);
    min_x = usable_bounds.x;
    min_y = usable_bounds.y;
    max_x = usable_bounds.x + SDL_max(usable_bounds.w - width, 0);
    max_y = usable_bounds.y + SDL_max(usable_bounds.h - height, 0);
    clamped_x = SDL_clamp(state->window_x, min_x, max_x);
    clamped_y = SDL_clamp(state->window_y, min_y, max_y);

    if (clamped_x != state->window_x || clamped_y != state->window_y) {
        state->window_x = clamped_x;
        state->window_y = clamped_y;
        SDL_SetWindowPosition(state->window, state->window_x, state->window_y);
    }
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
    const char *npu_model = selected_npu_model_id(&state->transcription.config);

    if (state->tray.control_widget != NULL) {
        SDL_SetTrayEntryChecked(state->tray.control_widget, state->control_mode == CONTROL_MODE_WIDGET);
    }
    if (state->tray.control_hotkey != NULL) {
        SDL_SetTrayEntryChecked(state->tray.control_hotkey, state->control_mode == CONTROL_MODE_HOTKEY);
    }
    if (state->tray.mode_standard != NULL) {
        SDL_SetTrayEntryChecked(state->tray.mode_standard, state->mode == APP_MODE_STANDARD);
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
    if (state->tray.backend_openai != NULL) {
        SDL_SetTrayEntryChecked(state->tray.backend_openai, state->transcription.config.backend == TB_TRANSCRIPTION_BACKEND_OPENAI);
    }
    if (state->tray.backend_npu != NULL) {
        SDL_SetTrayEntryChecked(state->tray.backend_npu, state->transcription.config.backend == TB_TRANSCRIPTION_BACKEND_NPU);
    }
    if (state->tray.npu_model_tiny != NULL) {
        SDL_SetTrayEntryChecked(state->tray.npu_model_tiny, SDL_strcmp(npu_model, "whisper_tiny_en") == 0);
    }
    if (state->tray.npu_model_base != NULL) {
        SDL_SetTrayEntryChecked(state->tray.npu_model_base, SDL_strcmp(npu_model, "whisper_base_en") == 0);
    }
    if (state->tray.npu_model_small != NULL) {
        SDL_SetTrayEntryChecked(state->tray.npu_model_small, SDL_strcmp(npu_model, "whisper_small_en") == 0);
    }
    if (state->tray.npu_model_turbo != NULL) {
        SDL_SetTrayEntryChecked(state->tray.npu_model_turbo, SDL_strcmp(npu_model, "whisper_large_v3_turbo") == 0);
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
    char model_label[96];
    char tooltip[160];
    const char *backend = NULL;

    if (state->tray.show_hide != NULL) {
        if (state->control_mode == CONTROL_MODE_HOTKEY) {
            SDL_SetTrayEntryLabel(state->tray.show_hide, "Hotkey Mode Active");
        } else {
            SDL_SetTrayEntryLabel(state->tray.show_hide, state->widget_visible ? "Hide Widget" : "Show Widget");
        }
    }

    if (state->tray.npu_model_parent != NULL) {
        SDL_snprintf(
            model_label,
            sizeof(model_label),
            "NPU Model (%s)",
            npu_model_label(selected_npu_model_id(&state->transcription.config))
        );
        SDL_SetTrayEntryLabel(state->tray.npu_model_parent, model_label);
    }

    backend = state->transcription.config.backend_name[0] != '\0'
        ? state->transcription.config.backend_name
        : transcription_backend_label(state->transcription.config.backend);

    if (state->tray.tray != NULL) {
        SDL_snprintf(
            tooltip,
            sizeof(tooltip),
            "terminal-buddy (%s backend, %s control, %s prompt, %s text%s%s)",
            backend,
            control_mode_label(state->control_mode),
            mode_label(state->mode)
            ,
            state->text_render_mode == TB_UI_TEXT_RENDER_ENGINE ? "engine" : "surface",
            state->control_mode == CONTROL_MODE_HOTKEY ? ", " : "",
            state->control_mode == CONTROL_MODE_HOTKEY ? hotkey_label() : ""
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

static void collapse_panel(AppState *state) {
    transcription_clear_feedback(state);
    clear_audio_visuals(state);
    resize_for_mode(state);
    apply_window_visibility_policy(state);
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
        apply_window_visibility_policy(state);
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
        apply_window_visibility_policy(state);
        request_redraw(state);
        return;
    }

    state->listening = true;
    resize_for_mode(state);
    apply_window_visibility_policy(state);
    request_redraw(state);
}

static void set_actual_window_visibility(AppState *state, bool visible) {
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
        SDL_HideWindow(state->window);
        SDL_SyncWindow(state->window);
    }

    update_tray_labels(state);
    request_redraw(state);
}

static void apply_window_visibility_policy(AppState *state) {
    set_actual_window_visibility(state, desired_window_visibility(state));
}

static void set_widget_visibility_preference(AppState *state, bool visible) {
    if (state->widget_visible == visible) {
        return;
    }

    state->widget_visible = visible;
    apply_window_visibility_policy(state);
    update_tray_labels(state);
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

static bool set_control_mode(AppState *state, ControlMode mode, bool persist) {
    ControlMode previous_mode = state->control_mode;

    if (state->control_mode == mode) {
        update_tray_checks(state);
        update_tray_labels(state);
        return true;
    }

    state->control_mode = mode;
    if (!update_hotkey_registration(state)) {
        state->control_mode = previous_mode;
        update_hotkey_registration(state);
        refresh_shell_state(state);
        return false;
    }

    apply_window_visibility_policy(state);
    refresh_shell_state(state);

    if (persist) {
        save_preferences(state);
    }

    return true;
}

static bool point_in_active_surface(const AppState *state, float x, float y) {
    if (is_panel_expanded(state)) {
        return x >= 0.0f
            && y >= 0.0f
            && x <= (float) state->window_metrics.pixel_width
            && y <= (float) state->window_metrics.pixel_height;
    }

    return tb_ui_bubble_contains(x, y) || tb_ui_terminal_button_contains(x, y);
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
    TapTarget tap_target = TAP_TARGET_PANEL;

    if (!point_in_active_surface(state, local_x, local_y)) {
        return;
    }

    if (tb_ui_terminal_button_contains(local_x, local_y)) {
        tap_target = TAP_TARGET_TERMINAL;
    }

    clamp_window_to_display(state);
    sync_window_position_from_os(state);
    state->drag.kind = kind;
    state->drag.finger_id = finger_id;
    state->drag.pressed = true;
    state->drag.dragging = false;
    state->drag.pending_toggle = true;
    state->drag.tap_target = tap_target;
    state->drag.start_global_x = global_x;
    state->drag.start_global_y = global_y;
    state->drag.window_start_x = state->window_x;
    state->drag.window_start_y = state->window_y;
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
            drag_perf_trace_begin(state);
        }
    }

    if (state->drag.dragging) {
        int next_window_x = state->drag.window_start_x + (int) SDL_roundf(delta_x);
        int next_window_y = state->drag.window_start_y + (int) SDL_roundf(delta_y);

        if (state->drag_perf.active) {
            state->drag_perf.pointer_update_count += 1;
        }

        if (next_window_x != state->window_x || next_window_y != state->window_y) {
            state->window_x = next_window_x;
            state->window_y = next_window_y;
            SDL_SetWindowPosition(state->window, state->window_x, state->window_y);
            if (state->drag_perf.active) {
                state->drag_perf.window_move_count += 1;
            }
        }
    }
}

static void handle_panel_tap(AppState *state);

static bool handle_feedback_primary_action_tap(AppState *state, float local_x, float local_y) {
    bool terminal_submit_ready = false;

    if (state == NULL || !tb_ui_primary_action_contains(local_x, local_y)) {
        return false;
    }

    SDL_LockMutex(state->transcription.mutex);
    terminal_submit_ready = state->transcription.terminal_submit_ready;
    SDL_UnlockMutex(state->transcription.mutex);

    if (!terminal_submit_ready) {
        return false;
    }

    if (try_submit_terminal_target(state)) {
        if (!register_pending_agent_launch(state)) {
            SDL_Log("Submitted the terminal command, but Buddy could not attach the launched agent target.");
        }
        collapse_panel(state);
    } else {
        SDL_LockMutex(state->transcription.mutex);
        SDL_snprintf(state->transcription.status_text, sizeof(state->transcription.status_text), "%s", "ENTER FAILED");
        SDL_UnlockMutex(state->transcription.mutex);
        request_redraw(state);
    }

    return true;
}

static void end_pointer_interaction(AppState *state, float local_x, float local_y) {
    bool should_toggle = state->drag.pressed && state->drag.pending_toggle && !state->drag.dragging;
    bool moved = state->drag.dragging;
    TapTarget tap_target = state->drag.tap_target;

    state->drag.kind = POINTER_NONE;
    state->drag.finger_id = 0;
    state->drag.pressed = false;
    state->drag.dragging = false;
    state->drag.pending_toggle = false;
    state->drag.tap_target = TAP_TARGET_NONE;

    if (should_toggle) {
        if (tap_target == TAP_TARGET_TERMINAL && tb_ui_terminal_button_contains(local_x, local_y)) {
#ifdef _WIN32
            launch_or_focus_terminal_companion(state);
#else
            transcription_set_ui(
                state,
                false,
                true,
                false,
                "TERMINAL UNSUPPORTED",
                "The embedded terminal launcher is only implemented on Windows right now."
            );
            resize_for_mode(state);
            apply_window_visibility_policy(state);
#endif
        } else if (!handle_feedback_primary_action_tap(state, local_x, local_y)) {
            handle_panel_tap(state);
        }
    }

    if (moved) {
        save_preferences(state);
        drag_perf_trace_end(state);
    }

    request_redraw(state);
}

static void render(AppState *state) {
    Uint64 render_counter_start = 0;
    Uint64 ticks_ms = SDL_GetTicks();
    float pulse = 0.5f + (0.5f * SDL_sinf((float) ticks_ms * 0.0065f));
    bool processing = false;
    bool show_feedback = false;
    bool terminal_submit_ready = false;
    char status[MAX_STATUS_TEXT];
    char metrics[MAX_METRICS_TEXT];
    char transcript[MAX_TRANSCRIPT_TEXT];
    TbUiModel model;

    SDL_zero(model);

    if (state->drag_perf.active) {
        render_counter_start = SDL_GetPerformanceCounter();
    }

    copy_transcription_snapshot(
        state,
        &processing,
        &show_feedback,
        &terminal_submit_ready,
        status,
        sizeof(status),
        metrics,
        sizeof(metrics),
        transcript,
        sizeof(transcript)
    );

    sync_window_metrics(state);
    model.window_width = state->window_metrics.pixel_width;
    model.window_height = state->window_metrics.pixel_height;
    model.ui_scale = state->window_metrics.ui_scale;

    if (state->listening) {
        model.scene = TB_UI_SCENE_LISTENING;
    } else if (processing) {
        model.scene = TB_UI_SCENE_PROCESSING;
    } else if (show_feedback) {
        model.scene = TB_UI_SCENE_FEEDBACK;
    } else {
        model.scene = TB_UI_SCENE_IDLE;
    }
    model.mode = (int) state->mode;
    model.terminal_button_active = state->mode == APP_MODE_TERMINAL;
    model.pulse = pulse;
    model.audio_level = state->audio.level;
    model.ticks_ms = ticks_ms;
    model.audio_history = state->audio.history;
    model.audio_history_count = AUDIO_HISTORY_COUNT;
    model.mode_label = mode_label(state->mode);
    model.backend_label = state->transcription.config.backend_name[0] != '\0' ? state->transcription.config.backend_name : NULL;
    model.status_text = status[0] != '\0' ? status : NULL;
    model.metrics_text = metrics[0] != '\0' ? metrics : NULL;
    model.transcript_text = transcript[0] != '\0' ? transcript : NULL;
    model.primary_action_label = terminal_submit_ready ? "Enter" : NULL;

    if (!tb_ui_render(&model)) {
        SDL_Log("tb_ui_render failed");
    }

    if (state->drag_perf.active && render_counter_start > 0) {
        Uint64 render_elapsed = SDL_GetPerformanceCounter() - render_counter_start;
        state->drag_perf.render_count += 1;
        state->drag_perf.render_total_counter += render_elapsed;
        if (render_elapsed > state->drag_perf.render_max_counter) {
            state->drag_perf.render_max_counter = render_elapsed;
        }
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

    if (state->drag.kind != POINTER_MOUSE) {
        return;
    }

    end_pointer_interaction(
        state,
        ui_x_from_window_x(state, button->x),
        ui_y_from_window_y(state, button->y)
    );
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
    if (state->drag.kind != POINTER_FINGER || state->drag.finger_id != finger->fingerID) {
        return;
    }

    end_pointer_interaction(
        state,
        ui_x_from_normalized_x(state, finger->x),
        ui_y_from_normalized_y(state, finger->y)
    );
}

static void SDLCALL on_tray_show_hide(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    if (state->control_mode == CONTROL_MODE_HOTKEY) {
        return;
    }

    set_widget_visibility_preference(state, !state->widget_visible);
}

static void SDLCALL on_tray_control_widget(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    set_control_mode(state, CONTROL_MODE_WIDGET, true);
}

static void SDLCALL on_tray_control_hotkey(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    set_control_mode(state, CONTROL_MODE_HOTKEY, true);
}

static void SDLCALL on_tray_backend_openai(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    switch_transcription_backend(state, TB_TRANSCRIPTION_BACKEND_OPENAI);
}

static void SDLCALL on_tray_backend_npu(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    switch_transcription_backend(state, TB_TRANSCRIPTION_BACKEND_NPU);
}

static void SDLCALL on_tray_npu_model_tiny(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    switch_npu_model(state, "whisper_tiny_en");
}

static void SDLCALL on_tray_npu_model_base(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    switch_npu_model(state, "whisper_base_en");
}

static void SDLCALL on_tray_npu_model_small(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    switch_npu_model(state, "whisper_small_en");
}

static void SDLCALL on_tray_npu_model_turbo(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    switch_npu_model(state, "whisper_large_v3_turbo");
}

static void SDLCALL on_tray_mode_standard(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState *) userdata;
    (void) entry;

    set_mode(state, APP_MODE_STANDARD, true);
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
    state->tray.control_parent = SDL_InsertTrayEntryAt(state->tray.menu, -1, "Control", SDL_TRAYENTRY_SUBMENU);
    state->tray.backend_parent = SDL_InsertTrayEntryAt(state->tray.menu, -1, "Backend", SDL_TRAYENTRY_SUBMENU);
    state->tray.npu_model_parent = SDL_InsertTrayEntryAt(state->tray.menu, -1, "NPU Model", SDL_TRAYENTRY_SUBMENU);
    state->tray.mode_parent = SDL_InsertTrayEntryAt(state->tray.menu, -1, "Prompt", SDL_TRAYENTRY_SUBMENU);
    state->tray.text_parent = SDL_InsertTrayEntryAt(state->tray.menu, -1, "Text", SDL_TRAYENTRY_SUBMENU);
    state->tray.quit = SDL_InsertTrayEntryAt(state->tray.menu, -1, "Quit", SDL_TRAYENTRY_BUTTON);

    if (
        state->tray.show_hide == NULL
        || state->tray.control_parent == NULL
        || state->tray.backend_parent == NULL
        || state->tray.npu_model_parent == NULL
        || state->tray.mode_parent == NULL
        || state->tray.text_parent == NULL
        || state->tray.quit == NULL
    ) {
        SDL_Log("Failed to create tray menu entries");
        return false;
    }

    state->tray.control_menu = SDL_CreateTraySubmenu(state->tray.control_parent);
    if (state->tray.control_menu == NULL) {
        SDL_Log("SDL_CreateTraySubmenu failed: %s", SDL_GetError());
        return false;
    }
    state->tray.backend_menu = SDL_CreateTraySubmenu(state->tray.backend_parent);
    if (state->tray.backend_menu == NULL) {
        SDL_Log("SDL_CreateTraySubmenu failed: %s", SDL_GetError());
        return false;
    }
    state->tray.npu_model_menu = SDL_CreateTraySubmenu(state->tray.npu_model_parent);
    if (state->tray.npu_model_menu == NULL) {
        SDL_Log("SDL_CreateTraySubmenu failed: %s", SDL_GetError());
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

    state->tray.control_widget = SDL_InsertTrayEntryAt(state->tray.control_menu, -1, "Widget", SDL_TRAYENTRY_CHECKBOX);
    state->tray.control_hotkey = SDL_InsertTrayEntryAt(state->tray.control_menu, -1, "Hotkey", SDL_TRAYENTRY_CHECKBOX);
    state->tray.backend_openai = SDL_InsertTrayEntryAt(state->tray.backend_menu, -1, "OpenAI", SDL_TRAYENTRY_CHECKBOX);
    state->tray.backend_npu = SDL_InsertTrayEntryAt(state->tray.backend_menu, -1, "NPU", SDL_TRAYENTRY_CHECKBOX);
    state->tray.npu_model_tiny = SDL_InsertTrayEntryAt(state->tray.npu_model_menu, -1, "Tiny", SDL_TRAYENTRY_CHECKBOX);
    state->tray.npu_model_base = SDL_InsertTrayEntryAt(state->tray.npu_model_menu, -1, "Base", SDL_TRAYENTRY_CHECKBOX);
    state->tray.npu_model_small = SDL_InsertTrayEntryAt(state->tray.npu_model_menu, -1, "Small", SDL_TRAYENTRY_CHECKBOX);
    state->tray.npu_model_turbo = SDL_InsertTrayEntryAt(state->tray.npu_model_menu, -1, "Large V3 Turbo", SDL_TRAYENTRY_CHECKBOX);
    state->tray.mode_standard = SDL_InsertTrayEntryAt(state->tray.mode_menu, -1, "Standard", SDL_TRAYENTRY_CHECKBOX);
    state->tray.mode_terminal = SDL_InsertTrayEntryAt(state->tray.mode_menu, -1, "Terminal", SDL_TRAYENTRY_CHECKBOX);
    state->tray.text_surface = SDL_InsertTrayEntryAt(state->tray.text_menu, -1, "Surface", SDL_TRAYENTRY_CHECKBOX);
    state->tray.text_engine = SDL_InsertTrayEntryAt(state->tray.text_menu, -1, "Engine", SDL_TRAYENTRY_CHECKBOX);
    state->tray.text_debug = SDL_InsertTrayEntryAt(state->tray.text_menu, -1, "Debug Logs", SDL_TRAYENTRY_CHECKBOX);

    if (
        state->tray.control_widget == NULL
        || state->tray.control_hotkey == NULL
        || state->tray.backend_openai == NULL
        || state->tray.backend_npu == NULL
        || state->tray.npu_model_tiny == NULL
        || state->tray.npu_model_base == NULL
        || state->tray.npu_model_small == NULL
        || state->tray.npu_model_turbo == NULL
        || state->tray.mode_standard == NULL
        || state->tray.mode_terminal == NULL
        || state->tray.text_surface == NULL
        || state->tray.text_engine == NULL
        || state->tray.text_debug == NULL
    ) {
        SDL_Log("Failed to create tray entries");
        return false;
    }

    SDL_SetTrayEntryCallback(state->tray.show_hide, on_tray_show_hide, state);
    SDL_SetTrayEntryCallback(state->tray.control_widget, on_tray_control_widget, state);
    SDL_SetTrayEntryCallback(state->tray.control_hotkey, on_tray_control_hotkey, state);
    SDL_SetTrayEntryCallback(state->tray.backend_openai, on_tray_backend_openai, state);
    SDL_SetTrayEntryCallback(state->tray.backend_npu, on_tray_backend_npu, state);
    SDL_SetTrayEntryCallback(state->tray.npu_model_tiny, on_tray_npu_model_tiny, state);
    SDL_SetTrayEntryCallback(state->tray.npu_model_base, on_tray_npu_model_base, state);
    SDL_SetTrayEntryCallback(state->tray.npu_model_small, on_tray_npu_model_small, state);
    SDL_SetTrayEntryCallback(state->tray.npu_model_turbo, on_tray_npu_model_turbo, state);
    SDL_SetTrayEntryCallback(state->tray.mode_standard, on_tray_mode_standard, state);
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

#ifdef _WIN32
static HWND get_app_hwnd(AppState *state) {
    if (state->app_hwnd == NULL && state->window != NULL) {
        SDL_PropertiesID props = SDL_GetWindowProperties(state->window);
        state->app_hwnd = (HWND) SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    }
    return state->app_hwnd;
}

static bool SDLCALL windows_message_hook(void *userdata, MSG *msg) {
    AppState *state = (AppState *) userdata;
    SDL_Event event;

    if (state == NULL || msg == NULL) {
        return true;
    }

    if (msg->message != WM_HOTKEY || (int) msg->wParam != TB_GLOBAL_HOTKEY_ID) {
        return true;
    }

    if (state->hotkey_event_type == 0 || state->hotkey_event_type == (Uint32) -1) {
        return false;
    }

    SDL_zero(event);
    event.type = state->hotkey_event_type;
    SDL_PushEvent(&event);
    return false;
}

static bool update_hotkey_registration(AppState *state) {
    bool should_register = false;

    if (state == NULL) {
        return false;
    }

    should_register = state->control_mode == CONTROL_MODE_HOTKEY;
    if (!should_register) {
        if (state->hotkey_registered) {
            UnregisterHotKey(NULL, TB_GLOBAL_HOTKEY_ID);
            state->hotkey_registered = false;
        }
        return true;
    }

    if (state->hotkey_registered) {
        return true;
    }

    if (!RegisterHotKey(NULL, TB_GLOBAL_HOTKEY_ID, TB_GLOBAL_HOTKEY_MODIFIERS, TB_GLOBAL_HOTKEY_VKEY)) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "HOTKEY UNAVAILABLE",
            "Ctrl+Alt+Space is already in use by another app."
        );
        resize_for_mode(state);
        apply_window_visibility_policy(state);
        request_redraw(state);
        return false;
    }

    state->hotkey_registered = true;
    return true;
}

static void update_last_external_window(AppState *state) {
    HWND foreground = GetForegroundWindow();
    HWND app_hwnd = get_app_hwnd(state);

    if (foreground != NULL && foreground != app_hwnd && IsWindow(foreground)) {
        state->last_external_hwnd = foreground;
    }
}

static bool utf8_from_wide(const wchar_t *input, char *out_text, size_t out_text_size) {
    int needed = 0;

    if (out_text == NULL || out_text_size == 0) {
        return false;
    }

    out_text[0] = '\0';
    if (input == NULL || input[0] == L'\0') {
        return false;
    }

    needed = WideCharToMultiByte(CP_UTF8, 0, input, -1, NULL, 0, NULL, NULL);
    if (needed <= 0 || (size_t) needed > out_text_size) {
        return false;
    }

    return WideCharToMultiByte(CP_UTF8, 0, input, -1, out_text, needed, NULL, NULL) > 0;
}

static bool wide_from_utf8(const char *input, wchar_t *out_text, size_t out_text_size) {
    int needed = 0;

    if (out_text == NULL || out_text_size == 0) {
        return false;
    }

    out_text[0] = L'\0';
    if (input == NULL || input[0] == '\0') {
        return false;
    }

    needed = MultiByteToWideChar(CP_UTF8, 0, input, -1, NULL, 0);
    if (needed <= 0 || (size_t) needed > out_text_size) {
        return false;
    }

    return MultiByteToWideChar(CP_UTF8, 0, input, -1, out_text, needed) > 0;
}

static HWND owned_terminal_hwnd(const AppState *state) {
    if (state == NULL || state->owned_terminal_window == NULL) {
        return NULL;
    }

    return (HWND) tb_owned_terminal_window_native_handle(state->owned_terminal_window);
}

static bool owned_terminal_matches_hwnd(const AppState *state, HWND target) {
    HWND owned = owned_terminal_hwnd(state);

    return target != NULL && owned != NULL && target == owned;
}

static bool owned_terminal_target_id_matches(const AppState *state, const char *target_id) {
    if (target_id == NULL || target_id[0] == '\0') {
        return false;
    }

    if (SDL_strcmp(target_id, TB_OWNED_TERMINAL_SHELL_TARGET_ID) == 0) {
        return true;
    }

    return state != NULL
        && state->owned_terminal_agent_target_id[0] != '\0'
        && SDL_strcmp(target_id, state->owned_terminal_agent_target_id) == 0;
}

static void clear_terminal_companion_tracking(AppState *state) {
    if (state == NULL) {
        return;
    }

    if (state->terminal_companion_process != NULL) {
        CloseHandle(state->terminal_companion_process);
        state->terminal_companion_process = NULL;
    }
    state->terminal_companion_pid = 0;
    state->terminal_companion_hwnd = NULL;
}

static void refresh_terminal_companion_tracking(AppState *state) {
    if (state == NULL || state->terminal_companion_process == NULL) {
        return;
    }

    if (WaitForSingleObject(state->terminal_companion_process, 0) == WAIT_TIMEOUT) {
        return;
    }

    clear_terminal_companion_tracking(state);
}

typedef struct TbWindowSearchContext {
    DWORD process_id;
    HWND window;
} TbWindowSearchContext;

static BOOL CALLBACK find_visible_window_for_process_cb(HWND window, LPARAM lparam) {
    TbWindowSearchContext *context = (TbWindowSearchContext *) lparam;
    DWORD process_id = 0;

    if (context == NULL || !IsWindow(window) || !IsWindowVisible(window)) {
        return TRUE;
    }
    if (GetWindow(window, GW_OWNER) != NULL) {
        return TRUE;
    }

    GetWindowThreadProcessId(window, &process_id);
    if (process_id != context->process_id) {
        return TRUE;
    }

    context->window = window;
    return FALSE;
}

static HWND find_visible_window_for_process(DWORD process_id) {
    TbWindowSearchContext context;

    SDL_zero(context);
    if (process_id == 0) {
        return NULL;
    }

    context.process_id = process_id;
    EnumWindows(find_visible_window_for_process_cb, (LPARAM) &context);
    return context.window;
}

static bool resolve_terminal_companion_path(char *out_path, size_t out_path_size) {
    const char *base_path = NULL;

    if (out_path == NULL || out_path_size == 0) {
        return false;
    }

    out_path[0] = '\0';
    base_path = SDL_GetBasePath();
    if (base_path == NULL) {
        return false;
    }

    SDL_snprintf(out_path, out_path_size, "%sterminal-buddy-terminal.exe", base_path);
    SDL_free((void *) base_path);
    return out_path[0] != '\0';
}

static bool terminal_companion_exists(const char *utf8_path) {
    wchar_t wide_path[MAX_PATH];
    DWORD attributes = 0;

    if (!wide_from_utf8(utf8_path, wide_path, SDL_arraysize(wide_path))) {
        return false;
    }

    attributes = GetFileAttributesW(wide_path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool focus_terminal_companion(AppState *state) {
    HWND window = NULL;

    if (state == NULL) {
        return false;
    }

    refresh_terminal_companion_tracking(state);
    window = state->terminal_companion_hwnd;
    if (window == NULL || !IsWindow(window)) {
        window = find_visible_window_for_process(state->terminal_companion_pid);
    }
    if (window == NULL || !IsWindow(window)) {
        return false;
    }

    state->terminal_companion_hwnd = window;
    state->last_external_hwnd = window;
    state->injection_target_hwnd = window;
    focus_window_best_effort(window);
    return true;
}

static bool launch_or_focus_terminal_companion(AppState *state) {
    char exe_path[TB_SIDECAR_PATH_MAX];
    wchar_t wide_exe_path[MAX_PATH];
    STARTUPINFOW startup_info;
    PROCESS_INFORMATION process_info;
    HWND launched_window = NULL;
    Uint64 deadline = 0;

    if (state == NULL) {
        return false;
    }

    set_mode(state, APP_MODE_TERMINAL, true);

    if (state->owned_terminal_window != NULL && tb_owned_terminal_window_is_available()) {
        if (tb_owned_terminal_window_show(state->owned_terminal_window)) {
            HWND owned_window = owned_terminal_hwnd(state);

            if (owned_window != NULL) {
                state->last_external_hwnd = owned_window;
                state->injection_target_hwnd = owned_window;
                focus_window_best_effort(owned_window);
                upsert_owned_terminal_shell_target(state, "idle");
            }
            request_redraw(state);
            return true;
        }
    }

    if (focus_terminal_companion(state)) {
        return true;
    }

    if (!resolve_terminal_companion_path(exe_path, sizeof(exe_path))) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "TERMINAL PATH FAILED",
            "Buddy could not resolve the terminal companion executable path."
        );
        resize_for_mode(state);
        apply_window_visibility_policy(state);
        return false;
    }

    if (!terminal_companion_exists(exe_path)) {
        char detail[MAX_TRANSCRIPT_TEXT];

        SDL_snprintf(
            detail,
            sizeof(detail),
            "Build the companion first:\ncmake --build build --target terminal-buddy-terminal\n\nExpected path:\n%s",
            exe_path
        );
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "TERMINAL NOT BUILT",
            detail
        );
        resize_for_mode(state);
        apply_window_visibility_policy(state);
        return false;
    }

    if (!wide_from_utf8(exe_path, wide_exe_path, SDL_arraysize(wide_exe_path))) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "TERMINAL PATH FAILED",
            exe_path
        );
        resize_for_mode(state);
        apply_window_visibility_policy(state);
        return false;
    }

    SDL_zero(startup_info);
    SDL_zero(process_info);
    startup_info.cb = sizeof(startup_info);

    if (!CreateProcessW(wide_exe_path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &startup_info, &process_info)) {
        char detail[MAX_TRANSCRIPT_TEXT];

        SDL_snprintf(
            detail,
            sizeof(detail),
            "CreateProcessW failed with error %lu while launching:\n%s",
            (unsigned long) GetLastError(),
            exe_path
        );
        transcription_set_ui(
            state,
            false,
            true,
            false,
            "TERMINAL LAUNCH FAILED",
            detail
        );
        resize_for_mode(state);
        apply_window_visibility_policy(state);
        return false;
    }

    CloseHandle(process_info.hThread);
    clear_terminal_companion_tracking(state);
    state->terminal_companion_process = process_info.hProcess;
    state->terminal_companion_pid = process_info.dwProcessId;
    state->terminal_companion_hwnd = NULL;

    deadline = SDL_GetTicks() + 5000u;
    while (SDL_GetTicks() < deadline) {
        launched_window = find_visible_window_for_process(state->terminal_companion_pid);
        if (launched_window != NULL) {
            break;
        }
        SDL_Delay(50);
    }

    if (launched_window != NULL) {
        state->terminal_companion_hwnd = launched_window;
        state->last_external_hwnd = launched_window;
        state->injection_target_hwnd = launched_window;
        focus_window_best_effort(launched_window);
    }

    request_redraw(state);
    return true;
}

static bool query_window_process_path_wide(HWND window, wchar_t *path, DWORD path_capacity, DWORD *process_id_out) {
    DWORD process_id = 0;
    HANDLE process = NULL;

    if (path == NULL || path_capacity == 0) {
        return false;
    }

    path[0] = L'\0';
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == 0) {
        return false;
    }

    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == NULL) {
        return false;
    }

    if (!QueryFullProcessImageNameW(process, 0, path, &path_capacity)) {
        CloseHandle(process);
        return false;
    }

    CloseHandle(process);
    if (process_id_out != NULL) {
        *process_id_out = process_id;
    }
    return true;
}

static bool query_window_process_metadata_utf8(
    HWND window,
    char *process_name_out,
    size_t process_name_out_size,
    char *process_dir_out,
    size_t process_dir_out_size,
    DWORD *process_id_out
) {
    wchar_t path[MAX_PATH];
    wchar_t directory[MAX_PATH];
    DWORD path_size = MAX_PATH;
    const wchar_t *basename = NULL;
    size_t directory_length = 0;

    if (process_name_out != NULL && process_name_out_size > 0) {
        process_name_out[0] = '\0';
    }
    if (process_dir_out != NULL && process_dir_out_size > 0) {
        process_dir_out[0] = '\0';
    }

    if (!query_window_process_path_wide(window, path, path_size, process_id_out)) {
        return false;
    }

    basename = wcsrchr(path, L'\\');
    basename = (basename != NULL) ? basename + 1 : path;
    if (process_name_out != NULL && process_name_out_size > 0) {
        utf8_from_wide(basename, process_name_out, process_name_out_size);
    }

    wcsncpy(directory, path, SDL_arraysize(directory) - 1);
    directory[SDL_arraysize(directory) - 1] = L'\0';
    directory_length = wcslen(directory);
    while (directory_length > 0 && directory[directory_length - 1] != L'\\' && directory[directory_length - 1] != L'/') {
        --directory_length;
    }
    if (directory_length > 0) {
        directory[directory_length - 1] = L'\0';
    } else {
        directory[0] = L'\0';
    }

    if (process_dir_out != NULL && process_dir_out_size > 0) {
        utf8_from_wide(directory, process_dir_out, process_dir_out_size);
    }
    return true;
}

static bool query_window_title_utf8(HWND window, char *title_out, size_t title_out_size) {
    wchar_t title[TB_SIDECAR_TEXT_MAX];

    if (title_out == NULL || title_out_size == 0) {
        return false;
    }

    title_out[0] = '\0';
    if (!IsWindow(window)) {
        return false;
    }

    if (GetWindowTextW(window, title, (int) SDL_arraysize(title)) <= 0) {
        return false;
    }

    return utf8_from_wide(title, title_out, title_out_size);
}

static bool upsert_owned_terminal_shell_target(AppState *state, const char *status) {
    HWND window = owned_terminal_hwnd(state);
    char process_name[TB_SIDECAR_TEXT_MAX];
    char process_dir[TB_SIDECAR_PATH_MAX];
    char label[TB_SIDECAR_TEXT_MAX];
    char window_id[TB_SIDECAR_ID_MAX];
    DWORD process_id = 0;
    const char *occupancy = "utility";
    const char *attached_agent_target_id = NULL;

    if (state == NULL || window == NULL || !IsWindow(window)) {
        return false;
    }

    process_name[0] = '\0';
    process_dir[0] = '\0';
    label[0] = '\0';
    window_id[0] = '\0';
    query_window_process_metadata_utf8(
        window,
        process_name,
        sizeof(process_name),
        process_dir,
        sizeof(process_dir),
        &process_id
    );
    SDL_snprintf(label, sizeof(label), "%s", tb_owned_terminal_window_title(state->owned_terminal_window));
    SDL_snprintf(window_id, sizeof(window_id), "win32:%p", (void *) window);

    if (state->owned_terminal_agent_target_id[0] != '\0') {
        occupancy = "agent_host";
        attached_agent_target_id = state->owned_terminal_agent_target_id;
    }

    return tb_sidecar_client_upsert_window_target(
        &state->sidecar,
        "observed_shell",
        "shell",
        TB_OWNED_TERMINAL_SHELL_TARGET_ID,
        TB_OWNED_TERMINAL_PROJECT_ID,
        process_dir,
        label,
        status != NULL && status[0] != '\0' ? status : "idle",
        "low",
        window_id,
        (Uint32) process_id,
        process_name,
        occupancy,
        attached_agent_target_id,
        NULL
    );
}

static void slugify_identifier(const char *text, char *out_text, size_t out_text_size) {
    bool previous_was_dash = false;
    size_t written = 0;
    const unsigned char *cursor = (const unsigned char *) text;

    if (out_text == NULL || out_text_size == 0) {
        return;
    }

    out_text[0] = '\0';
    if (text == NULL) {
        return;
    }

    while (*cursor != '\0' && written + 1 < out_text_size) {
        unsigned char ch = *cursor++;

        if (ch >= 'A' && ch <= 'Z') {
            out_text[written++] = (char) (ch - 'A' + 'a');
            previous_was_dash = false;
            continue;
        }
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            out_text[written++] = (char) ch;
            previous_was_dash = false;
            continue;
        }
        if (!previous_was_dash && written > 0) {
            out_text[written++] = '-';
            previous_was_dash = true;
        }
    }

    while (written > 0 && out_text[written - 1] == '-') {
        --written;
    }
    out_text[written] = '\0';
}

static bool contains_substring_ci(const char *text, const char *pattern) {
    size_t pattern_length = 0;
    size_t offset = 0;

    if (text == NULL || pattern == NULL || pattern[0] == '\0') {
        return false;
    }

    pattern_length = SDL_strlen(pattern);
    while (text[offset] != '\0') {
        size_t index = 0;
        while (
            index < pattern_length
            && text[offset + index] != '\0'
            && tolower((unsigned char) text[offset + index]) == tolower((unsigned char) pattern[index])
        ) {
            ++index;
        }
        if (index == pattern_length) {
            return true;
        }
        ++offset;
    }

    return false;
}

static bool copy_trimmed_range(const char *start, size_t length, char *out_text, size_t out_text_size) {
    size_t begin = 0;
    size_t end = length;
    size_t write_length = 0;

    if (out_text == NULL || out_text_size == 0) {
        return false;
    }

    out_text[0] = '\0';
    if (start == NULL || length == 0) {
        return false;
    }

    while (begin < length && isspace((unsigned char) start[begin])) {
        ++begin;
    }
    while (end > begin && isspace((unsigned char) start[end - 1])) {
        --end;
    }
    if (end <= begin) {
        return false;
    }

    write_length = end - begin;
    if (write_length >= out_text_size) {
        write_length = out_text_size - 1;
    }
    SDL_memcpy(out_text, start + begin, write_length);
    out_text[write_length] = '\0';
    return write_length > 0;
}

static bool is_generic_project_label(const char *text) {
    char slug[TB_SIDECAR_ID_MAX];
    static const char *generic_slugs[] = {
        "claude",
        "codex",
        "powershell",
        "pwsh",
        "terminal",
        "windows-terminal",
        "command-prompt",
        "cmd",
        "shell",
        "administrator",
        "admin"
    };
    size_t index = 0;

    slugify_identifier(text, slug, sizeof(slug));
    if (slug[0] == '\0') {
        return true;
    }

    for (index = 0; index < SDL_arraysize(generic_slugs); ++index) {
        if (SDL_strcmp(slug, generic_slugs[index]) == 0) {
            return true;
        }
    }

    return false;
}

static void infer_project_label(const char *title, const char *process_name, char *out_text, size_t out_text_size) {
    static const char *separators[] = {" - ", " | ", " : "};
    size_t index = 0;

    if (out_text == NULL || out_text_size == 0) {
        return;
    }

    out_text[0] = '\0';
    if (title != NULL && title[0] != '\0') {
        for (index = 0; index < SDL_arraysize(separators); ++index) {
            const char *separator = separators[index];
            const char *found = SDL_strstr(title, separator);
            char left[TB_SIDECAR_TEXT_MAX];
            char right[TB_SIDECAR_TEXT_MAX];

            if (found == NULL) {
                continue;
            }

            left[0] = '\0';
            right[0] = '\0';
            copy_trimmed_range(title, (size_t) (found - title), left, sizeof(left));
            copy_trimmed_range(
                found + SDL_strlen(separator),
                SDL_strlen(found + SDL_strlen(separator)),
                right,
                sizeof(right)
            );

            if (left[0] != '\0' && !is_generic_project_label(left)) {
                SDL_snprintf(out_text, out_text_size, "%s", left);
                return;
            }
            if (right[0] != '\0' && !is_generic_project_label(right)) {
                SDL_snprintf(out_text, out_text_size, "%s", right);
                return;
            }
        }

        if (!is_generic_project_label(title)) {
            SDL_snprintf(out_text, out_text_size, "%s", title);
            return;
        }
    }

    if (process_name != NULL && process_name[0] != '\0' && !is_generic_project_label(process_name)) {
        SDL_snprintf(out_text, out_text_size, "%s", process_name);
    }
}

static void classify_observed_target(
    const char *title,
    const char *process_name,
    const char **target_kind_out,
    const char **provider_out
) {
    const char *target_kind = "observed_shell";
    const char *provider = "shell";

    if (contains_substring_ci(title, "claude") || contains_substring_ci(process_name, "claude")) {
        target_kind = "observed_agent";
        provider = "claude";
    } else if (contains_substring_ci(title, "codex") || contains_substring_ci(process_name, "codex")) {
        target_kind = "observed_agent";
        provider = "codex";
    }

    if (target_kind_out != NULL) {
        *target_kind_out = target_kind;
    }
    if (provider_out != NULL) {
        *provider_out = provider;
    }
}

static HWND target_id_to_hwnd(const char *target_id) {
    const char *marker = NULL;
    unsigned long long raw = 0;

    if (target_id == NULL || target_id[0] == '\0') {
        return NULL;
    }

    marker = SDL_strstr(target_id, ":win32:");
    if (marker == NULL) {
        return NULL;
    }

    marker += SDL_strlen(":win32:");
    if (SDL_sscanf(marker, "%llx", &raw) != 1) {
        return NULL;
    }

    return (HWND) (uintptr_t) raw;
}

static bool target_id_looks_shell(const char *target_id) {
    return target_id != NULL && SDL_strncmp(target_id, "shell:", 6) == 0;
}

static bool window_process_is_windows_terminal(HWND window) {
    char process_name[TB_SIDECAR_TEXT_MAX];

    process_name[0] = '\0';
    if (!query_window_process_metadata_utf8(window, process_name, sizeof(process_name), NULL, 0, NULL)) {
        return false;
    }

    return SDL_strcasecmp(process_name, "WindowsTerminal.exe") == 0;
}

static HWND resolve_injection_target_hwnd(const AppState *state) {
    HWND target = NULL;

    if (state == NULL) {
        return NULL;
    }

    target = state->injection_target_hwnd;
    if (target == NULL || !IsWindow(target)) {
        target = state->last_external_hwnd;
    }

    if (target == NULL || !IsWindow(target)) {
        return NULL;
    }

    return target;
}

static void populate_sidecar_context(
    AppState *state,
    char *project_id_out,
    size_t project_id_size,
    char *target_id_out,
    size_t target_id_size
) {
    HWND target = NULL;
    DWORD process_id = 0;
    const char *target_kind = "observed_shell";
    const char *provider = "shell";
    char title[TB_SIDECAR_TEXT_MAX];
    char process_name[TB_SIDECAR_TEXT_MAX];
    char process_dir[TB_SIDECAR_PATH_MAX];
    char project_label[TB_SIDECAR_TEXT_MAX];
    char label[TB_SIDECAR_TEXT_MAX];
    char window_id[TB_SIDECAR_ID_MAX];

    if (project_id_out != NULL && project_id_size > 0) {
        project_id_out[0] = '\0';
    }
    if (target_id_out != NULL && target_id_size > 0) {
        target_id_out[0] = '\0';
    }
    if (state == NULL) {
        return;
    }

    target = resolve_injection_target_hwnd(state);
    if (target == NULL || !IsWindow(target)) {
        return;
    }

    if (owned_terminal_matches_hwnd(state, target)) {
        if (project_id_out != NULL && project_id_size > 0) {
            SDL_snprintf(project_id_out, project_id_size, "%s", TB_OWNED_TERMINAL_PROJECT_ID);
        }
        if (target_id_out != NULL && target_id_size > 0) {
            SDL_snprintf(target_id_out, target_id_size, "%s", TB_OWNED_TERMINAL_SHELL_TARGET_ID);
        }
        upsert_owned_terminal_shell_target(state, "idle");
        return;
    }

    title[0] = '\0';
    process_name[0] = '\0';
    process_dir[0] = '\0';
    project_label[0] = '\0';
    label[0] = '\0';
    window_id[0] = '\0';
    query_window_title_utf8(target, title, sizeof(title));
    query_window_process_metadata_utf8(
        target,
        process_name,
        sizeof(process_name),
        process_dir,
        sizeof(process_dir),
        &process_id
    );
    classify_observed_target(title, process_name, &target_kind, &provider);

    if (title[0] != '\0') {
        SDL_snprintf(label, sizeof(label), "%s", title);
    } else if (SDL_strcmp(provider, "claude") == 0) {
        SDL_snprintf(label, sizeof(label), "Claude session");
    } else if (SDL_strcmp(provider, "codex") == 0) {
        SDL_snprintf(label, sizeof(label), "Codex session");
    } else if (process_name[0] != '\0') {
        SDL_snprintf(label, sizeof(label), "%s", process_name);
    } else {
        SDL_snprintf(label, sizeof(label), "External shell");
    }

    infer_project_label(title, process_name, project_label, sizeof(project_label));
    if (project_id_out != NULL && project_id_size > 0) {
        slugify_identifier(project_label[0] != '\0' ? project_label : (title[0] != '\0' ? title : process_name), project_id_out, project_id_size);
        if (project_id_out[0] == '\0') {
            SDL_snprintf(project_id_out, project_id_size, "desktop-shell");
        }
    }

    if (target_id_out != NULL && target_id_size > 0) {
        SDL_snprintf(target_id_out, target_id_size, "%s:win32:%p", provider, (void *) target);
    }
    SDL_snprintf(window_id, sizeof(window_id), "win32:%p", (void *) target);

    if (
        project_id_out != NULL
        && project_id_out[0] != '\0'
        && target_id_out != NULL
        && target_id_out[0] != '\0'
    ) {
        if (!tb_sidecar_client_upsert_observed_target(
                &state->sidecar,
                target_kind,
                provider,
                target_id_out,
                project_id_out,
                process_dir,
                label,
                window_id,
                (Uint32) process_id,
                process_name
            )) {
            target_id_out[0] = '\0';
        }
    }
}

static bool register_pending_agent_launch(AppState *state) {
    HWND target = NULL;
    DWORD process_id = 0;
    char provider[TB_SIDECAR_CATEGORY_MAX];
    char project_id[TB_SIDECAR_ID_MAX];
    char shell_target_id[TB_SIDECAR_ID_MAX];
    char title[TB_SIDECAR_TEXT_MAX];
    char process_name[TB_SIDECAR_TEXT_MAX];
    char process_dir[TB_SIDECAR_PATH_MAX];
    char shell_label[TB_SIDECAR_TEXT_MAX];
    char project_label[TB_SIDECAR_TEXT_MAX];
    char window_id[TB_SIDECAR_ID_MAX];
    bool pending_launch = false;

    if (state == NULL || state->transcription.mutex == NULL) {
        return false;
    }

    provider[0] = '\0';
    project_id[0] = '\0';
    shell_target_id[0] = '\0';
    SDL_LockMutex(state->transcription.mutex);
    pending_launch = state->transcription.pending_agent_launch;
    if (pending_launch) {
        SDL_snprintf(provider, sizeof(provider), "%s", state->transcription.pending_agent_provider);
        SDL_snprintf(project_id, sizeof(project_id), "%s", state->transcription.pending_agent_project_id);
        SDL_snprintf(shell_target_id, sizeof(shell_target_id), "%s", state->transcription.pending_agent_shell_target_id);
    }
    SDL_UnlockMutex(state->transcription.mutex);

    if (!pending_launch) {
        return true;
    }

    if (SDL_strcmp(provider, "claude") != 0 && SDL_strcmp(provider, "codex") != 0) {
        return false;
    }

    if (SDL_strcmp(shell_target_id, TB_OWNED_TERMINAL_SHELL_TARGET_ID) == 0) {
        HWND window = owned_terminal_hwnd(state);
        char owned_process_name[TB_SIDECAR_TEXT_MAX];
        char owned_process_dir[TB_SIDECAR_PATH_MAX];
        char label[TB_SIDECAR_TEXT_MAX];
        char owned_window_id[TB_SIDECAR_ID_MAX];
        DWORD local_process_id = 0;

        if (window == NULL || !IsWindow(window)) {
            return false;
        }

        owned_process_name[0] = '\0';
        owned_process_dir[0] = '\0';
        label[0] = '\0';
        owned_window_id[0] = '\0';
        query_window_process_metadata_utf8(
            window,
            owned_process_name,
            sizeof(owned_process_name),
            owned_process_dir,
            sizeof(owned_process_dir),
            &local_process_id
        );
        SDL_snprintf(label, sizeof(label), "%s", tb_owned_terminal_window_title(state->owned_terminal_window));
        SDL_snprintf(owned_window_id, sizeof(owned_window_id), "win32:%p", (void *) window);
        SDL_snprintf(
            state->owned_terminal_agent_provider,
            sizeof(state->owned_terminal_agent_provider),
            "%s",
            provider
        );
        SDL_snprintf(
            state->owned_terminal_agent_target_id,
            sizeof(state->owned_terminal_agent_target_id),
            "%s:buddy-terminal",
            provider
        );

        if (!tb_sidecar_client_upsert_window_target(
                &state->sidecar,
                "observed_agent",
                provider,
                state->owned_terminal_agent_target_id,
                project_id[0] != '\0' ? project_id : TB_OWNED_TERMINAL_PROJECT_ID,
                owned_process_dir,
                SDL_strcmp(provider, "claude") == 0 ? "Claude session" : "Codex session",
                "launching_agent",
                "low",
                owned_window_id,
                (Uint32) local_process_id,
                owned_process_name,
                NULL,
                NULL,
                TB_OWNED_TERMINAL_SHELL_TARGET_ID
            )) {
            return false;
        }

        return upsert_owned_terminal_shell_target(state, "launching_agent");
    }

    target = target_id_to_hwnd(shell_target_id);
    if (target == NULL || !IsWindow(target)) {
        target = resolve_injection_target_hwnd(state);
    }
    if (target == NULL || !IsWindow(target)) {
        return false;
    }

    title[0] = '\0';
    process_name[0] = '\0';
    process_dir[0] = '\0';
    shell_label[0] = '\0';
    project_label[0] = '\0';
    window_id[0] = '\0';
    query_window_title_utf8(target, title, sizeof(title));
    query_window_process_metadata_utf8(
        target,
        process_name,
        sizeof(process_name),
        process_dir,
        sizeof(process_dir),
        &process_id
    );

    if (title[0] != '\0') {
        SDL_snprintf(shell_label, sizeof(shell_label), "%s", title);
    } else if (process_name[0] != '\0') {
        SDL_snprintf(shell_label, sizeof(shell_label), "%s", process_name);
    } else {
        SDL_snprintf(shell_label, sizeof(shell_label), "External shell");
    }

    if (project_id[0] == '\0') {
        infer_project_label(title, process_name, project_label, sizeof(project_label));
        slugify_identifier(
            project_label[0] != '\0' ? project_label : (title[0] != '\0' ? title : process_name),
            project_id,
            sizeof(project_id)
        );
        if (project_id[0] == '\0') {
            SDL_snprintf(project_id, sizeof(project_id), "desktop-shell");
        }
    }

    SDL_snprintf(window_id, sizeof(window_id), "win32:%p", (void *) target);
    if (!target_id_looks_shell(shell_target_id)) {
        SDL_snprintf(shell_target_id, sizeof(shell_target_id), "shell:%s", window_id);
    }

    return tb_sidecar_client_register_observed_agent_launch(
        &state->sidecar,
        provider,
        shell_target_id,
        project_id,
        process_dir,
        shell_label,
        window_id,
        (Uint32) process_id,
        process_name
    );
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

static bool send_virtual_key(WORD virtual_key) {
    INPUT inputs[2];

    SDL_zeroa(inputs);

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = virtual_key;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = virtual_key;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    return SendInput(SDL_arraysize(inputs), inputs, sizeof(INPUT)) == SDL_arraysize(inputs);
}

static bool injection_target_is_windows_terminal(const AppState *state) {
    HWND target = resolve_injection_target_hwnd(state);

    if (owned_terminal_matches_hwnd(state, target)) {
        return true;
    }

    return target != NULL && window_process_is_windows_terminal(target);
}

static bool try_inject_transcript(AppState *state, const char *transcript) {
    HWND target = resolve_injection_target_hwnd(state);

    if (target == NULL || !IsWindow(target)) {
        return false;
    }
    if (transcript == NULL || transcript[0] == '\0') {
        return false;
    }

    if (owned_terminal_matches_hwnd(state, target)) {
        return state->owned_terminal_window != NULL
            && tb_owned_terminal_window_submit_text(state->owned_terminal_window, transcript, false);
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

static bool try_submit_terminal_target(AppState *state) {
    HWND target = resolve_injection_target_hwnd(state);

    if (owned_terminal_matches_hwnd(state, target)) {
        return state->owned_terminal_window != NULL
            && tb_owned_terminal_window_send_enter(state->owned_terminal_window);
    }

    if (target == NULL || !window_process_is_windows_terminal(target)) {
        return false;
    }

    if (!focus_window_best_effort(target)) {
        return false;
    }

    SDL_Delay(40);
    return send_virtual_key(VK_RETURN);
}
#else
static bool update_hotkey_registration(AppState *state) {
    (void) state;
    return true;
}

static void update_last_external_window(AppState *state) {
    (void) state;
}

static bool try_inject_transcript(AppState *state, const char *transcript) {
    (void) state;
    (void) transcript;
    return false;
}

static bool injection_target_is_windows_terminal(const AppState *state) {
    (void) state;
    return false;
}

static bool try_submit_terminal_target(AppState *state) {
    (void) state;
    return false;
}

static bool target_id_looks_shell(const char *target_id) {
    (void) target_id;
    return false;
}

static bool register_pending_agent_launch(AppState *state) {
    (void) state;
    return true;
}

static void populate_sidecar_context(
    AppState *state,
    char *project_id_out,
    size_t project_id_size,
    char *target_id_out,
    size_t target_id_size
) {
    (void) state;
    if (project_id_out != NULL && project_id_size > 0) {
        project_id_out[0] = '\0';
    }
    if (target_id_out != NULL && target_id_size > 0) {
        target_id_out[0] = '\0';
    }
}
#endif

static int transcription_worker(void *userdata) {
    TranscriptionJob *job = (TranscriptionJob *) userdata;
    AppState *state = job->app;
    char *response_text = NULL;
    char *error_text = NULL;
    char metrics_text[MAX_METRICS_TEXT];
    TbTranscriptionRequest request;
    TbTranscriptionNpuTimingStats timing_stats;

    SDL_zeroa(metrics_text);
    SDL_zero(timing_stats);

    request.samples = job->samples;
    request.sample_count = job->sample_count;
    request.sample_rate = AUDIO_SAMPLE_RATE;
    request.prompt = mode_prompt(job->mode);

    if (!tb_transcription_execute(&job->config, &request, &response_text, &error_text)) {
        if (error_text == NULL) {
            error_text = SDL_strdup("Transcription request failed");
        }
    }

    if (tb_transcription_backend_npu_get_last_timing_stats(&timing_stats)) {
        format_transcription_metrics_text(&job->config, &timing_stats, metrics_text, sizeof(metrics_text));
    }

    SDL_LockMutex(state->transcription.mutex);
    state->transcription.processing = false;
    state->transcription.show_feedback = true;
    state->transcription.terminal_submit_ready = false;
    clear_pending_agent_launch_locked(&state->transcription);
    state->transcription.worker_done = true;
    state->transcription.sidecar_detail_text[0] = '\0';
    if (response_text != NULL) {
        SDL_snprintf(state->transcription.status_text, sizeof(state->transcription.status_text), "%s", "TRANSCRIPT READY");
        SDL_snprintf(state->transcription.transcript_text, sizeof(state->transcription.transcript_text), "%s", response_text);
        state->transcription.clipboard_dirty = true;
    } else {
        SDL_snprintf(state->transcription.status_text, sizeof(state->transcription.status_text), "%s", "TRANSCRIPTION FAILED");
        SDL_snprintf(state->transcription.transcript_text, sizeof(state->transcription.transcript_text), "%s", error_text != NULL ? error_text : "Unknown error");
    }
    SDL_snprintf(state->transcription.metrics_text, sizeof(state->transcription.metrics_text), "%s", metrics_text);
    SDL_UnlockMutex(state->transcription.mutex);

    SDL_free(response_text);
    SDL_free(error_text);
    SDL_free(job->samples);
    SDL_free(job);
    return 0;
}

static bool begin_transcription(AppState *state) {
    TranscriptionJob *job = NULL;
    float *sample_copy = NULL;

    reload_transcription_config(state);
    if (!state->transcription.config.ready) {
        transcription_set_ui(
            state,
            false,
            true,
            false,
            state->transcription.config.missing_status,
            state->transcription.config.missing_detail
        );
        resize_for_mode(state);
        apply_window_visibility_policy(state);
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
        apply_window_visibility_policy(state);
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
        apply_window_visibility_policy(state);
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
        apply_window_visibility_policy(state);
        return false;
    }

    job->app = state;
    job->samples = sample_copy;
    job->sample_count = state->audio.captured_count;
    job->mode = state->mode;
    job->config = state->transcription.config;

    SDL_LockMutex(state->transcription.mutex);
    state->transcription.processing = true;
    state->transcription.worker_done = false;
    state->transcription.show_feedback = true;
    state->transcription.clipboard_dirty = false;
    state->transcription.terminal_submit_ready = false;
    SDL_snprintf(state->transcription.status_text, sizeof(state->transcription.status_text), "%s", "TRANSCRIBING");
    state->transcription.transcript_text[0] = '\0';
    state->transcription.metrics_text[0] = '\0';
    state->transcription.sidecar_detail_text[0] = '\0';
    SDL_UnlockMutex(state->transcription.mutex);

    state->transcription.worker = SDL_CreateThread(transcription_worker, "transcription-worker", job);
    if (state->transcription.worker == NULL) {
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
        apply_window_visibility_policy(state);
        return false;
    }

    resize_for_mode(state);
    apply_window_visibility_policy(state);
    return true;
}

static void handle_panel_tap(AppState *state) {
    bool processing = false;
    bool show_feedback = false;

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
        return;
    }

    if (show_feedback) {
        collapse_panel(state);
        return;
    }

    start_listening(state);
}

static void handle_hotkey_action(AppState *state) {
    if (state == NULL || state->control_mode != CONTROL_MODE_HOTKEY) {
        return;
    }

    update_last_external_window(state);
    handle_panel_tap(state);
}

static void pump_transcription_results(AppState *state) {
    bool should_reap = false;
    bool should_copy = false;
    bool terminal_submit_ready = false;
    char transcript[MAX_TRANSCRIPT_TEXT];
    char active_project_id[TB_SIDECAR_ID_MAX];
    char active_target_id[TB_SIDECAR_ID_MAX];
    TbSidecarRouteDecision route_decision;
    TbSidecarProjectSummary project_summary;
    TbSidecarTargetSnapshot target_snapshot;
    const char *text_to_inject = transcript;
    const char *status_text = NULL;
    const char *pending_launch_project_id = NULL;
    const char *pending_launch_shell_target_id = NULL;
    bool routed = false;
    bool injected = false;
    bool summary_ready = false;
    bool snapshot_ready = false;
    bool should_arm_pending_launch = false;

    SDL_zero(route_decision);
    SDL_zero(project_summary);
    SDL_zero(target_snapshot);

    SDL_LockMutex(state->transcription.mutex);
    should_reap = state->transcription.worker_done && state->transcription.worker != NULL;
    should_copy = state->transcription.clipboard_dirty;
    SDL_snprintf(transcript, sizeof(transcript), "%s", state->transcription.transcript_text);
    if (should_copy) {
        state->transcription.clipboard_dirty = false;
    }
    SDL_UnlockMutex(state->transcription.mutex);

    if (should_copy && transcript[0] != '\0') {
        populate_sidecar_context(
            state,
            active_project_id,
            sizeof(active_project_id),
            active_target_id,
            sizeof(active_target_id)
        );

        if (state->mode == APP_MODE_TERMINAL) {
            routed = tb_sidecar_client_route_utterance(
                &state->sidecar,
                transcript,
                active_project_id[0] != '\0' ? active_project_id : NULL,
                active_target_id[0] != '\0' ? active_target_id : NULL,
                250,
                &route_decision
            );
            if (routed) {
                if (route_decision.has_command_text) {
                    text_to_inject = route_decision.command_text;
                } else {
                    text_to_inject = NULL;
                }
#ifdef _WIN32
                if (route_decision.selected_target_id[0] != '\0') {
                    HWND routed_target = target_id_to_hwnd(route_decision.selected_target_id);
                    if (owned_terminal_target_id_matches(state, route_decision.selected_target_id)) {
                        HWND owned_target = owned_terminal_hwnd(state);

                        if (owned_target != NULL && IsWindow(owned_target)) {
                            state->injection_target_hwnd = owned_target;
                        } else {
                            text_to_inject = NULL;
                        }
                    } else if (routed_target != NULL && IsWindow(routed_target)) {
                        state->injection_target_hwnd = routed_target;
                    } else if (
                        active_target_id[0] != '\0'
                        && SDL_strcmp(route_decision.selected_target_id, active_target_id) != 0
                    ) {
                        text_to_inject = NULL;
                    }
                }
#endif
            }

            if (routed && SDL_strcmp(route_decision.category, "fleet_meta") == 0) {
                const char *summary_project_id = route_decision.selected_project_id[0] != '\0'
                    ? route_decision.selected_project_id
                    : (active_project_id[0] != '\0' ? active_project_id : NULL);
                summary_ready = tb_sidecar_client_request_project_summary(
                    &state->sidecar,
                    summary_project_id,
                    false,
                    300,
                    &project_summary
                );
            } else if (
                routed
                && route_decision.selected_target_id[0] != '\0'
                && SDL_strcmp(route_decision.category, "host_control") == 0
            ) {
                snapshot_ready = tb_sidecar_client_request_target_snapshot(
                    &state->sidecar,
                    route_decision.selected_target_id,
                    200,
                    &target_snapshot
                );
            } else if (
                routed
                && route_decision.selected_target_id[0] != '\0'
                && SDL_strcmp(route_decision.category, "target_directed") == 0
            ) {
                snapshot_ready = tb_sidecar_client_request_target_snapshot(
                    &state->sidecar,
                    route_decision.selected_target_id,
                    200,
                    &target_snapshot
                );
            }
        } else {
            tb_sidecar_client_submit_utterance(
                &state->sidecar,
                transcript,
                active_project_id[0] != '\0' ? active_project_id : NULL,
                active_target_id[0] != '\0' ? active_target_id : NULL
            );
        }

        if (text_to_inject != NULL && text_to_inject[0] != '\0') {
            terminal_submit_ready = state->mode == APP_MODE_TERMINAL && injection_target_is_windows_terminal(state);
            injected = try_inject_transcript(state, text_to_inject);
        }

        if (routed) {
            if (SDL_strcmp(route_decision.category, "host_control") == 0) {
                if (SDL_strcmp(route_decision.host_action_kind, "launch_agent") == 0) {
                    if (route_decision.has_command_text) {
                        status_text = (terminal_submit_ready && injected) ? "AGENT LAUNCH READY" : (injected ? "PASTED AGENT LAUNCH" : "AGENT LAUNCH PLANNED");
                    } else {
                        status_text = snapshot_ready ? "AGENT LAUNCH READY" : "AGENT LAUNCH NEEDED";
                    }
                } else if (route_decision.has_command_text) {
                    status_text = (terminal_submit_ready && injected) ? "COMMAND READY" : (injected ? "PASTED COMMAND" : "COMMAND PLANNED");
                } else {
                    status_text = snapshot_ready ? "HOST ACTION READY" : "HOST ACTION NEEDED";
                }
            } else if (SDL_strcmp(route_decision.category, "target_directed") == 0) {
                if (route_decision.has_command_text) {
                    status_text = (terminal_submit_ready && injected) ? "AGENT MESSAGE READY" : (injected ? "PASTED TO AGENT" : "AGENT TARGETED");
                } else {
                    status_text = snapshot_ready ? "AGENT CONTEXT READY" : "AGENT TARGET NEEDED";
                }
            } else if (SDL_strcmp(route_decision.category, "fleet_meta") == 0) {
                status_text = summary_ready ? "SUMMARY READY" : "SUMMARY REQUEST READY";
            } else if (SDL_strcmp(route_decision.category, "ambiguous") == 0) {
                status_text = "NEEDS CLARIFICATION";
            }
        }

        if (status_text == NULL) {
            status_text = (terminal_submit_ready && injected) ? "PASTED TO TERMINAL" : (injected ? "PASTED TO TARGET" : "TRANSCRIPT READY");
        }

        should_arm_pending_launch =
            routed
            && terminal_submit_ready
            && injected
            && SDL_strcmp(route_decision.category, "host_control") == 0
            && SDL_strcmp(route_decision.host_action_kind, "launch_agent") == 0
            && route_decision.agent_provider[0] != '\0';
        pending_launch_project_id = route_decision.selected_project_id[0] != '\0'
            ? route_decision.selected_project_id
            : (active_project_id[0] != '\0' ? active_project_id : NULL);
        pending_launch_shell_target_id = route_decision.selected_target_id[0] != '\0'
            ? route_decision.selected_target_id
            : (target_id_looks_shell(active_target_id) ? active_target_id : NULL);

        SDL_LockMutex(state->transcription.mutex);
        state->transcription.terminal_submit_ready = terminal_submit_ready && injected;
        clear_pending_agent_launch_locked(&state->transcription);
        if (should_arm_pending_launch) {
            state->transcription.pending_agent_launch = true;
            SDL_snprintf(
                state->transcription.pending_agent_provider,
                sizeof(state->transcription.pending_agent_provider),
                "%s",
                route_decision.agent_provider
            );
            SDL_snprintf(
                state->transcription.pending_agent_project_id,
                sizeof(state->transcription.pending_agent_project_id),
                "%s",
                pending_launch_project_id != NULL ? pending_launch_project_id : ""
            );
            SDL_snprintf(
                state->transcription.pending_agent_shell_target_id,
                sizeof(state->transcription.pending_agent_shell_target_id),
                "%s",
                pending_launch_shell_target_id != NULL ? pending_launch_shell_target_id : ""
            );
        }
        SDL_snprintf(
            state->transcription.status_text,
            sizeof(state->transcription.status_text),
            "%s",
            status_text
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

    tb_transcription_backend_npu_shutdown();

    if (state->transcription.mutex != NULL) {
        SDL_DestroyMutex(state->transcription.mutex);
        state->transcription.mutex = NULL;
    }

    SDL_free(state->transcription.env_path);
    state->transcription.env_path = NULL;
}

int main(int argc, char **argv) {
    AppState state;

    (void) argc;
    (void) argv;
    SDL_zero(state);
    state.running = true;
    state.visible = true;
    state.needs_redraw = true;
    state.perf_logging_enabled = perf_logging_from_env();
    tb_sidecar_client_init(&state.sidecar);
    state.owned_terminal_window = tb_owned_terminal_window_create();
    state.transcription.mutex = SDL_CreateMutex();

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
    state.perf_counter_freq = SDL_GetPerformanceFrequency();
    state.hotkey_event_type = SDL_RegisterEvents(1);
    if (state.hotkey_event_type == (Uint32) -1) {
        state.hotkey_event_type = 0;
    }

    load_preferences(&state);
    reload_transcription_config(&state);
    if (!tb_sidecar_client_start(&state.sidecar, TERMINAL_BUDDY_SOURCE_DIR)) {
        SDL_Log("Continuing without sidecar routing");
    }

    state.window = SDL_CreateWindow(
        "terminal-buddy",
        IDLE_WINDOW_WIDTH,
        IDLE_WINDOW_HEIGHT,
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
    clamp_window_to_display(&state);
#ifdef _WIN32
    state.app_hwnd = get_app_hwnd(&state);
    SDL_SetWindowsMessageHook(windows_message_hook, &state);
    if (!update_hotkey_registration(&state)) {
        state.control_mode = CONTROL_MODE_WIDGET;
    }
#endif

    tb_ui_set_text_render_mode(state.text_render_mode);
    tb_ui_set_text_debug_logging(state.text_debug_logging);
    set_log_capture_enabled(state.text_debug_logging);
    setup_perf_logging(&state);

    initialize_audio(&state);

    if (!setup_tray(&state)) {
        SDL_Log("Continuing without tray integration");
    }

    apply_window_visibility_policy(&state);
    refresh_shell_state(&state);

    while (state.running) {
        bool processing = false;
        bool animated = false;
        Uint64 now_ms = 0;
        Uint64 frame_interval_ms = 33;
        Uint32 sleep_ms = 50;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == state.hotkey_event_type) {
                handle_hotkey_action(&state);
                continue;
            }
            if (
                state.owned_terminal_window != NULL
                && tb_owned_terminal_window_handles_event(state.owned_terminal_window, &event)
            ) {
#ifdef _WIN32
                HWND owned_window = owned_terminal_hwnd(&state);

                if (owned_window != NULL) {
                    state.last_external_hwnd = owned_window;
                    if (state.mode == APP_MODE_TERMINAL) {
                        state.injection_target_hwnd = owned_window;
                    }
                    upsert_owned_terminal_shell_target(&state, "idle");
                }
#endif
                continue;
            }

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
                    if (state.drag_perf.active) {
                        state.drag_perf.window_exposed_count += 1;
                    }
                    if (!state.drag.dragging) {
                        request_redraw(&state);
                    }
                    break;
                case SDL_EVENT_WINDOW_MOVED:
                    sync_window_position_from_os(&state);
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    sync_window_metrics(&state);
                    clamp_window_to_display(&state);
                    request_redraw(&state);
                    break;
                case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                    sync_window_metrics(&state);
                    resize_for_mode(&state);
                    clamp_window_to_display(&state);
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
        pump_transcription_results(&state);
        refresh_sidecar_feedback(&state);
        if (state.owned_terminal_window != NULL) {
            tb_owned_terminal_window_tick(state.owned_terminal_window);
        }

        processing = ui_is_processing(&state);
        animated = (state.listening || processing) && !state.drag.dragging;
        now_ms = SDL_GetTicks();

        if (!processing) {
            tb_transcription_backend_npu_pump(now_ms, state.transcription.config.npu_cache_idle_ms);
        }

        if (state.visible) {
            if (animated && (now_ms - state.last_render_ms >= frame_interval_ms)) {
                request_redraw(&state);
            }

            if (state.needs_redraw && !state.drag.dragging) {
                render(&state);
            }
        } else {
            sleep_ms = 50;
        }

        if (state.visible) {
            if (state.drag.pressed) {
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

        if (state.owned_terminal_window != NULL && tb_owned_terminal_window_has_window(state.owned_terminal_window)) {
            sleep_ms = SDL_min(sleep_ms, 16u);
        }

        SDL_Delay(sleep_ms);
    }

    if (state.listening) {
        stop_listening(&state);
    }

#ifdef _WIN32
    SDL_SetWindowsMessageHook(NULL, NULL);
    if (state.hotkey_registered) {
        UnregisterHotKey(NULL, TB_GLOBAL_HOTKEY_ID);
        state.hotkey_registered = false;
    }
#endif

    save_preferences(&state);
    destroy_tray(&state);
    destroy_audio(&state);
    tb_sidecar_client_shutdown(&state.sidecar);
#ifdef _WIN32
    clear_terminal_companion_tracking(&state);
#endif
    destroy_transcription_state(&state);
    tb_owned_terminal_window_destroy(state.owned_terminal_window);
    tb_ui_shutdown();
    shutdown_perf_logging();

    if (state.prefs_path != NULL) {
        SDL_free(state.prefs_path);
    }

    SDL_DestroyRenderer(state.renderer);
    SDL_DestroyWindow(state.window);
    shutdown_log_capture();
    SDL_Quit();
    return 0;
}
