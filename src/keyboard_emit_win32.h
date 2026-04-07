#ifndef TERMINAL_BUDDY_KEYBOARD_EMIT_WIN32_H
#define TERMINAL_BUDDY_KEYBOARD_EMIT_WIN32_H

#include <stdbool.h>

#include "keyboard_layout.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool tb_keyboard_emit_key(HWND target, const TbKeyboardKeySpec *spec, const TbKeyboardModState *mods);
#else
typedef void *HWND;
bool tb_keyboard_emit_key(HWND target, const TbKeyboardKeySpec *spec, const TbKeyboardModState *mods);
#endif

#endif
