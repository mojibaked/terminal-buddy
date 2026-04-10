#ifndef TERMINAL_BUDDY_MANAGED_TERMINAL_H
#define TERMINAL_BUDDY_MANAGED_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TB_MANAGED_TERMINAL_TITLE_MAX 256

typedef struct TbManagedTerminal TbManagedTerminal;

typedef struct TbManagedTerminalOptions {
    uint16_t cols;
    uint16_t rows;
    uint32_t cell_width;
    uint32_t cell_height;
    size_t max_scrollback;
    const wchar_t *startup_command;
} TbManagedTerminalOptions;

typedef struct TbManagedTerminalStatus {
    bool child_alive;
    bool child_exited;
    bool has_pending_output;
    int exit_code;
    uint16_t cols;
    uint16_t rows;
    uint32_t cell_width;
    uint32_t cell_height;
    char title[TB_MANAGED_TERMINAL_TITLE_MAX];
} TbManagedTerminalStatus;

void tb_managed_terminal_options_defaults(TbManagedTerminalOptions *options);
TbManagedTerminal *tb_managed_terminal_create(const TbManagedTerminalOptions *options);
void tb_managed_terminal_destroy(TbManagedTerminal *terminal);
bool tb_managed_terminal_resize(
    TbManagedTerminal *terminal,
    uint16_t cols,
    uint16_t rows,
    uint32_t cell_width,
    uint32_t cell_height
);
bool tb_managed_terminal_pump_output(TbManagedTerminal *terminal, bool *out_had_output);
bool tb_managed_terminal_has_pending_output(const TbManagedTerminal *terminal);
bool tb_managed_terminal_write(TbManagedTerminal *terminal, const char *text, size_t len);
bool tb_managed_terminal_write_text(TbManagedTerminal *terminal, const char *text);
bool tb_managed_terminal_snapshot_text_alloc(
    TbManagedTerminal *terminal,
    char **out_text,
    size_t *out_len
);
bool tb_managed_terminal_take_title_update(
    TbManagedTerminal *terminal,
    const char **out_title
);
void tb_managed_terminal_fill_status(
    const TbManagedTerminal *terminal,
    TbManagedTerminalStatus *out_status
);

#ifdef __cplusplus
}
#endif

#endif
