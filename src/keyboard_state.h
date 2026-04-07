#ifndef TERMINAL_BUDDY_KEYBOARD_STATE_H
#define TERMINAL_BUDDY_KEYBOARD_STATE_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#define TB_KEYBOARD_SHIFT_DOUBLE_TAP_MS 400u
#define TB_KEYBOARD_REPEAT_INITIAL_MS 450u
#define TB_KEYBOARD_REPEAT_INTERVAL_MS 75u

typedef struct TbKeyboardModState {
    bool shift;
    bool caps_lock;
    bool ctrl;
    bool alt;
    Uint64 last_shift_tap_ms;
} TbKeyboardModState;

void tb_keyboard_mod_init(TbKeyboardModState *state);
void tb_keyboard_toggle_shift(TbKeyboardModState *state, Uint64 now_ms);
void tb_keyboard_toggle_ctrl(TbKeyboardModState *state);
void tb_keyboard_toggle_alt(TbKeyboardModState *state);
void tb_keyboard_auto_release(TbKeyboardModState *state);
char tb_keyboard_apply_shift_char(const TbKeyboardModState *state, char ch, char shift_ch);

#endif
