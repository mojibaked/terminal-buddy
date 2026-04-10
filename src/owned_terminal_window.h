#ifndef TERMINAL_BUDDY_OWNED_TERMINAL_WINDOW_H
#define TERMINAL_BUDDY_OWNED_TERMINAL_WINDOW_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TbOwnedTerminalWindow TbOwnedTerminalWindow;

TbOwnedTerminalWindow *tb_owned_terminal_window_create(void);
void tb_owned_terminal_window_destroy(TbOwnedTerminalWindow *window);

bool tb_owned_terminal_window_is_available(void);
bool tb_owned_terminal_window_show(TbOwnedTerminalWindow *window);
bool tb_owned_terminal_window_focus(TbOwnedTerminalWindow *window);
bool tb_owned_terminal_window_has_window(const TbOwnedTerminalWindow *window);
bool tb_owned_terminal_window_is_visible(const TbOwnedTerminalWindow *window);
bool tb_owned_terminal_window_handles_event(
    TbOwnedTerminalWindow *window,
    const SDL_Event *event
);
void tb_owned_terminal_window_tick(TbOwnedTerminalWindow *window);

bool tb_owned_terminal_window_submit_text(
    TbOwnedTerminalWindow *window,
    const char *text,
    bool submit
);
bool tb_owned_terminal_window_send_enter(TbOwnedTerminalWindow *window);

void *tb_owned_terminal_window_native_handle(
    const TbOwnedTerminalWindow *window
);
const char *tb_owned_terminal_window_title(
    const TbOwnedTerminalWindow *window
);

#ifdef __cplusplus
}
#endif

#endif
