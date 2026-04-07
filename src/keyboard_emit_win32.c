#include "keyboard_emit_win32.h"

#include <stddef.h>

#include <SDL3/SDL.h>

#ifdef _WIN32
static bool tb_focus_window_best_effort(HWND target) {
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

static bool tb_send_inputs(INPUT *inputs, UINT count) {
    return SendInput(count, inputs, sizeof(INPUT)) == count;
}

static bool tb_send_unicode_char(wchar_t ch) {
    INPUT inputs[2];

    SDL_zeroa(inputs);
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = ch;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    return tb_send_inputs(inputs, 2);
}

static bool tb_send_vk_combo(WORD vk, bool shift, bool ctrl, bool alt) {
    INPUT inputs[8];
    UINT count = 0;

    SDL_zeroa(inputs);

    if (ctrl) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_CONTROL;
        ++count;
    }
    if (alt) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_MENU;
        ++count;
    }
    if (shift) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        ++count;
    }

    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = vk;
    ++count;

    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = vk;
    inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
    ++count;

    if (shift) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        ++count;
    }
    if (alt) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_MENU;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        ++count;
    }
    if (ctrl) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_CONTROL;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        ++count;
    }

    return tb_send_inputs(inputs, count);
}

static bool tb_emit_character(const TbKeyboardKeySpec *spec, const TbKeyboardModState *mods) {
    char buffer[2];
    wchar_t wide = 0;

    if (spec == NULL || spec->output == NULL || spec->output[0] == '\0' || spec->output[1] != '\0') {
        return false;
    }

    buffer[0] = tb_keyboard_apply_shift_char(
        mods,
        spec->output[0],
        (spec->shift_label != NULL && spec->shift_label[0] != '\0' && spec->shift_label[1] == '\0') ? spec->shift_label[0] : '\0'
    );
    buffer[1] = '\0';
    wide = (wchar_t) (unsigned char) buffer[0];

    if (!mods->ctrl && !mods->alt) {
        return tb_send_unicode_char(wide);
    }

    SHORT mapped = VkKeyScanW(wide);
    if (mapped == -1) {
        return false;
    }

    return tb_send_vk_combo(
        (WORD) (mapped & 0xff),
        (mapped & 0x0100) != 0,
        mods->ctrl,
        mods->alt
    );
}

static bool tb_emit_special(TbKeyboardAction action, const TbKeyboardModState *mods) {
    WORD vk = 0;

    switch (action) {
        case TB_KEYBOARD_ACTION_SPACE:
            vk = VK_SPACE;
            break;
        case TB_KEYBOARD_ACTION_BACKSPACE:
            vk = VK_BACK;
            break;
        case TB_KEYBOARD_ACTION_ENTER:
            vk = VK_RETURN;
            break;
        case TB_KEYBOARD_ACTION_TAB:
            vk = VK_TAB;
            break;
        case TB_KEYBOARD_ACTION_ESCAPE:
            vk = VK_ESCAPE;
            break;
        case TB_KEYBOARD_ACTION_LEFT:
            vk = VK_LEFT;
            break;
        case TB_KEYBOARD_ACTION_RIGHT:
            vk = VK_RIGHT;
            break;
        case TB_KEYBOARD_ACTION_UP:
            vk = VK_UP;
            break;
        case TB_KEYBOARD_ACTION_DOWN:
            vk = VK_DOWN;
            break;
        case TB_KEYBOARD_ACTION_HOME:
            vk = VK_HOME;
            break;
        case TB_KEYBOARD_ACTION_END:
            vk = VK_END;
            break;
        case TB_KEYBOARD_ACTION_PAGE_UP:
            vk = VK_PRIOR;
            break;
        case TB_KEYBOARD_ACTION_PAGE_DOWN:
            vk = VK_NEXT;
            break;
        default:
            return false;
    }

    return tb_send_vk_combo(vk, mods->shift || mods->caps_lock, mods->ctrl, mods->alt);
}

bool tb_keyboard_emit_key(HWND target, const TbKeyboardKeySpec *spec, const TbKeyboardModState *mods) {
    if (target == NULL || !IsWindow(target) || spec == NULL || mods == NULL) {
        return false;
    }

    if (!tb_focus_window_best_effort(target)) {
        return false;
    }

    switch (spec->action) {
        case TB_KEYBOARD_ACTION_CHAR:
            return tb_emit_character(spec, mods);
        case TB_KEYBOARD_ACTION_SPACE:
        case TB_KEYBOARD_ACTION_BACKSPACE:
        case TB_KEYBOARD_ACTION_ENTER:
        case TB_KEYBOARD_ACTION_TAB:
        case TB_KEYBOARD_ACTION_ESCAPE:
        case TB_KEYBOARD_ACTION_LEFT:
        case TB_KEYBOARD_ACTION_RIGHT:
        case TB_KEYBOARD_ACTION_UP:
        case TB_KEYBOARD_ACTION_DOWN:
        case TB_KEYBOARD_ACTION_HOME:
        case TB_KEYBOARD_ACTION_END:
        case TB_KEYBOARD_ACTION_PAGE_UP:
        case TB_KEYBOARD_ACTION_PAGE_DOWN:
            return tb_emit_special(spec->action, mods);
        default:
            return false;
    }
}
#else
bool tb_keyboard_emit_key(HWND target, const TbKeyboardKeySpec *spec, const TbKeyboardModState *mods) {
    (void) target;
    (void) spec;
    (void) mods;
    return false;
}
#endif
