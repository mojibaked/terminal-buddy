#ifndef TERMINAL_BUDDY_UI_CLAY_INTERNAL_H
#define TERMINAL_BUDDY_UI_CLAY_INTERNAL_H

#include <SDL3/SDL.h>

#include "ui_clay.h"
#include "../third_party/clay/clay.h"

#define TB_UI_FONT_ID_BODY 0
#define TB_UI_BUTTON_SIZE 76
#define TB_UI_BUTTON_GLOW_SIZE 84
#define TB_UI_SHELL_WIDTH 348
#define TB_UI_SHELL_HEIGHT 112
#define TB_UI_SHELL_RADIUS 50

typedef struct TbUiColors {
    Clay_Color accent;
    Clay_Color glow;
} TbUiColors;

static inline Clay_String tb_ui_string(const char *text) {
    Clay_String result = {0};

    if (text == NULL) {
        result.chars = "";
        result.length = 0;
        return result;
    }

    result.chars = text;
    result.length = (int32_t) SDL_strlen(text);
    result.isStaticallyAllocated = false;
    return result;
}

static inline Clay_Color tb_ui_color(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    return (Clay_Color) {r, g, b, a};
}

static inline float tb_ui_scale_value(const TbUiModel *model, float value) {
    float scale = 1.0f;

    if (model != NULL && model->ui_scale > 0.01f) {
        scale = model->ui_scale;
    }

    return value * scale;
}

static inline uint16_t tb_ui_scale_u16(const TbUiModel *model, float value) {
    return (uint16_t) SDL_roundf(tb_ui_scale_value(model, value));
}

static inline float tb_ui_button_size(const TbUiModel *model) {
    return tb_ui_scale_value(model, TB_UI_BUTTON_SIZE);
}

static inline float tb_ui_button_glow_size(const TbUiModel *model) {
    return tb_ui_scale_value(model, TB_UI_BUTTON_GLOW_SIZE);
}

static inline float tb_ui_shell_width(const TbUiModel *model) {
    return tb_ui_scale_value(model, TB_UI_SHELL_WIDTH);
}

static inline float tb_ui_shell_height(const TbUiModel *model) {
    return tb_ui_scale_value(model, TB_UI_SHELL_HEIGHT);
}

static inline float tb_ui_shell_radius(const TbUiModel *model) {
    return tb_ui_scale_value(model, TB_UI_SHELL_RADIUS);
}

static inline TbUiColors tb_ui_colors_for_mode(int mode) {
    switch (mode) {
        case 1:
            return (TbUiColors) {
                .accent = {82, 155, 255, 255},
                .glow = {85, 111, 255, 82}
            };
        case 2:
            return (TbUiColors) {
                .accent = {255, 171, 64, 255},
                .glow = {255, 111, 76, 82}
            };
        case 0:
        default:
            return (TbUiColors) {
                .accent = {42, 212, 138, 255},
                .glow = {64, 79, 255, 72}
            };
    }
}

static inline Clay_Color tb_ui_active_red(void) {
    return tb_ui_color(232, 72, 72, 255);
}

static inline Clay_Color tb_ui_active_glow(void) {
    return tb_ui_color(255, 116, 116, 110);
}

void tb_ui_build_button(const TbUiModel *model, bool active);
void tb_ui_build_expanded_shell(const TbUiModel *model);
void tb_ui_build_idle_shell(const TbUiModel *model);
void tb_ui_build_layout(const TbUiModel *model);

#endif
