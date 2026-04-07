#include "ui_clay.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#define CLAY_IMPLEMENTATION
#include "ui_clay_internal.h"
#include "../third_party/clay/clay_renderer_SDL3.c"

typedef struct TbUiState {
    bool ready;
    bool ttf_ready;
    Clay_Arena arena;
    void *arena_memory;
    Clay_SDL3RendererData renderer_data;
    TTF_Font *fonts[1];
} TbUiState;

static TbUiState g_ui;

static Clay_Dimensions tb_ui_measure_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data) {
    TTF_Font **fonts = (TTF_Font **) user_data;
    TTF_Font *font = NULL;
    int width = 0;
    int height = 0;

    if (fonts == NULL || config == NULL) {
        return (Clay_Dimensions) {0};
    }

    font = fonts[config->fontId];
    if (font == NULL) {
        return (Clay_Dimensions) {0};
    }

    TTF_SetFontSize(font, config->fontSize);
    if (!TTF_GetStringSize(font, text.chars, text.length, &width, &height)) {
        SDL_Log("TTF_GetStringSize failed: %s", SDL_GetError());
        return (Clay_Dimensions) {0};
    }

    if (config->lineHeight > 0 && config->lineHeight > height) {
        height = config->lineHeight;
    }

    return (Clay_Dimensions) { (float) width, (float) height };
}

static void tb_ui_log_clay_error(Clay_ErrorData error_data) {
    SDL_Log("Clay error: %.*s", (int) error_data.errorText.length, error_data.errorText.chars);
}

static TTF_Font *tb_ui_open_default_font(void) {
    static const char *candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf"
    };

    for (int index = 0; index < (int) SDL_arraysize(candidates); ++index) {
        TTF_Font *font = TTF_OpenFont(candidates[index], 16.0f);
        if (font != NULL) {
            return font;
        }
    }

    return NULL;
}

static TbUiTextRenderMode tb_ui_text_mode_from_env(void) {
    const char *value = SDL_getenv("TB_UI_TEXT_MODE");

    if (value != NULL && SDL_strcasecmp(value, "engine") == 0) {
        return TB_UI_TEXT_RENDER_ENGINE;
    }

    return TB_UI_TEXT_RENDER_SURFACE;
}

static bool tb_ui_text_logging_from_env(void) {
    const char *value = SDL_getenv("TB_UI_TEXT_DEBUG");

    return value != NULL
        && (SDL_strcmp(value, "1") == 0 || SDL_strcasecmp(value, "true") == 0 || SDL_strcasecmp(value, "yes") == 0);
}

bool tb_ui_init(SDL_Renderer *renderer) {
    uint32_t min_memory = 0;

    if (g_ui.ready) {
        return true;
    }

    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return false;
    }
    g_ui.ttf_ready = true;

    g_ui.fonts[TB_UI_FONT_ID_BODY] = tb_ui_open_default_font();
    if (g_ui.fonts[TB_UI_FONT_ID_BODY] == NULL) {
        SDL_Log("Failed to open a usable system font: %s", SDL_GetError());
        tb_ui_shutdown();
        return false;
    }

    g_ui.renderer_data.renderer = renderer;
    g_ui.renderer_data.textRenderMode = (int) tb_ui_text_mode_from_env();
    g_ui.renderer_data.textDebugLogging = tb_ui_text_logging_from_env();
    g_ui.renderer_data.textEngine = TTF_CreateRendererTextEngine(renderer);
    if (g_ui.renderer_data.textEngine == NULL) {
        SDL_Log("TTF_CreateRendererTextEngine failed: %s", SDL_GetError());
        tb_ui_shutdown();
        return false;
    }
    g_ui.renderer_data.fonts = g_ui.fonts;

    min_memory = Clay_MinMemorySize();
    g_ui.arena_memory = SDL_malloc(min_memory);
    if (g_ui.arena_memory == NULL) {
        SDL_Log("Failed to allocate Clay arena");
        tb_ui_shutdown();
        return false;
    }

    g_ui.arena = Clay_CreateArenaWithCapacityAndMemory(min_memory, g_ui.arena_memory);
    Clay_Initialize(
        g_ui.arena,
        (Clay_Dimensions) {TB_UI_SHELL_WIDTH, TB_UI_SHELL_HEIGHT},
        (Clay_ErrorHandler) { tb_ui_log_clay_error }
    );
    Clay_SetMeasureTextFunction(tb_ui_measure_text, g_ui.fonts);

    g_ui.ready = true;
    return true;
}

void tb_ui_set_text_render_mode(TbUiTextRenderMode mode) {
    g_ui.renderer_data.textRenderMode = (int) mode;
}

TbUiTextRenderMode tb_ui_get_text_render_mode(void) {
    return (TbUiTextRenderMode) g_ui.renderer_data.textRenderMode;
}

void tb_ui_set_text_debug_logging(bool enabled) {
    g_ui.renderer_data.textDebugLogging = enabled;
}

bool tb_ui_get_text_debug_logging(void) {
    return g_ui.renderer_data.textDebugLogging;
}

void tb_ui_shutdown(void) {
    if (g_ui.renderer_data.textEngine != NULL) {
        TTF_DestroyRendererTextEngine(g_ui.renderer_data.textEngine);
        g_ui.renderer_data.textEngine = NULL;
    }

    if (g_ui.fonts[TB_UI_FONT_ID_BODY] != NULL) {
        TTF_CloseFont(g_ui.fonts[TB_UI_FONT_ID_BODY]);
        g_ui.fonts[TB_UI_FONT_ID_BODY] = NULL;
    }

    SDL_free(g_ui.arena_memory);
    g_ui.arena_memory = NULL;

    if (g_ui.ttf_ready) {
        TTF_Quit();
        g_ui.ttf_ready = false;
    }

    SDL_zero(g_ui);
}

bool tb_ui_render(const TbUiModel *model) {
    Clay_RenderCommandArray render_commands;

    if (!g_ui.ready || model == NULL) {
        return false;
    }

    Clay_SetLayoutDimensions((Clay_Dimensions) {
        (float) model->window_width,
        (float) model->window_height
    });

    Clay_BeginLayout();
    tb_ui_build_layout(model);
    render_commands = Clay_EndLayout(1.0f / 60.0f);

    SDL_SetRenderDrawBlendMode(g_ui.renderer_data.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ui.renderer_data.renderer, 0, 0, 0, 0);
    SDL_RenderClear(g_ui.renderer_data.renderer);
    SDL_Clay_RenderClayCommands(&g_ui.renderer_data, &render_commands);
    SDL_RenderPresent(g_ui.renderer_data.renderer);
    return true;
}
