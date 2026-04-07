#ifndef TERMINAL_BUDDY_KEYBOARD_LAYOUT_H
#define TERMINAL_BUDDY_KEYBOARD_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL.h>

#include "keyboard_state.h"

#define TB_KEYBOARD_WINDOW_WIDTH 980
#define TB_KEYBOARD_WINDOW_HEIGHT 392
#define TB_KEYBOARD_MAX_KEYS 64

typedef enum TbKeyboardAction {
    TB_KEYBOARD_ACTION_CHAR = 0,
    TB_KEYBOARD_ACTION_SPACE,
    TB_KEYBOARD_ACTION_BACKSPACE,
    TB_KEYBOARD_ACTION_ENTER,
    TB_KEYBOARD_ACTION_TAB,
    TB_KEYBOARD_ACTION_ESCAPE,
    TB_KEYBOARD_ACTION_LEFT,
    TB_KEYBOARD_ACTION_RIGHT,
    TB_KEYBOARD_ACTION_UP,
    TB_KEYBOARD_ACTION_DOWN,
    TB_KEYBOARD_ACTION_HOME,
    TB_KEYBOARD_ACTION_END,
    TB_KEYBOARD_ACTION_PAGE_UP,
    TB_KEYBOARD_ACTION_PAGE_DOWN,
    TB_KEYBOARD_ACTION_SHIFT,
    TB_KEYBOARD_ACTION_CTRL,
    TB_KEYBOARD_ACTION_ALT
} TbKeyboardAction;

typedef struct TbKeyboardKeySpec {
    const char *id;
    const char *label;
    const char *shift_label;
    const char *output;
    TbKeyboardAction action;
    float weight;
    bool repeatable;
} TbKeyboardKeySpec;

typedef struct TbKeyboardKeyRect {
    const TbKeyboardKeySpec *spec;
    SDL_FRect rect;
    int index;
} TbKeyboardKeyRect;

typedef struct TbKeyboardLayoutResult {
    SDL_FRect shell_rect;
    SDL_FRect bubble_rect;
    SDL_FRect header_rect;
    SDL_FRect subtitle_rect;
    SDL_FRect modifier_rect;
    SDL_FRect keyboard_rect;
    TbKeyboardKeyRect keys[TB_KEYBOARD_MAX_KEYS];
    int key_count;
} TbKeyboardLayoutResult;

bool tb_keyboard_build_layout(int pixel_width, int pixel_height, float ui_scale, TbKeyboardLayoutResult *out_layout);
const TbKeyboardKeyRect *tb_keyboard_hit_test(const TbKeyboardLayoutResult *layout, float x, float y);
const TbKeyboardKeyRect *tb_keyboard_key_rect_at(const TbKeyboardLayoutResult *layout, int index);
bool tb_keyboard_spec_is_modifier(const TbKeyboardKeySpec *spec);
bool tb_keyboard_spec_is_active(const TbKeyboardKeySpec *spec, const TbKeyboardModState *mods);
const char *tb_keyboard_display_label(
    const TbKeyboardKeySpec *spec,
    const TbKeyboardModState *mods,
    char *buffer,
    size_t buffer_size
);

#endif
