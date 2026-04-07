#include "keyboard_state.h"

#include <ctype.h>

void tb_keyboard_mod_init(TbKeyboardModState *state) {
    if (state == NULL) {
        return;
    }

    state->shift = false;
    state->caps_lock = false;
    state->ctrl = false;
    state->alt = false;
    state->last_shift_tap_ms = 0;
}

void tb_keyboard_toggle_shift(TbKeyboardModState *state, Uint64 now_ms) {
    Uint64 delta_ms = 0;

    if (state == NULL) {
        return;
    }

    if (state->caps_lock) {
        state->caps_lock = false;
        state->shift = false;
        state->last_shift_tap_ms = 0;
        return;
    }

    if (state->last_shift_tap_ms > 0 && now_ms > state->last_shift_tap_ms) {
        delta_ms = now_ms - state->last_shift_tap_ms;
    }

    if (delta_ms > 0 && delta_ms <= TB_KEYBOARD_SHIFT_DOUBLE_TAP_MS) {
        state->caps_lock = true;
        state->shift = false;
        state->last_shift_tap_ms = 0;
        return;
    }

    state->shift = !state->shift;
    state->last_shift_tap_ms = now_ms;
}

void tb_keyboard_toggle_ctrl(TbKeyboardModState *state) {
    if (state != NULL) {
        state->ctrl = !state->ctrl;
    }
}

void tb_keyboard_toggle_alt(TbKeyboardModState *state) {
    if (state != NULL) {
        state->alt = !state->alt;
    }
}

void tb_keyboard_auto_release(TbKeyboardModState *state) {
    if (state == NULL) {
        return;
    }

    if (!state->caps_lock) {
        state->shift = false;
    }
    state->ctrl = false;
    state->alt = false;
}

char tb_keyboard_apply_shift_char(const TbKeyboardModState *state, char ch, char shift_ch) {
    if (state == NULL) {
        return ch;
    }

    if (state->shift && shift_ch != '\0') {
        return shift_ch;
    }

    if ((state->shift || state->caps_lock) && ch >= 'a' && ch <= 'z') {
        return (char) toupper((unsigned char) ch);
    }

    return ch;
}
