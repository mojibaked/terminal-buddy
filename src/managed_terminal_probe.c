#include "managed_terminal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void print_usage(const char *program_name) {
    printf("Usage: %s [--pump-ms N] [--send TEXT]\n", program_name);
    printf("Environment overrides:\n");
    printf("  TB_MANAGED_TERMINAL_COMMAND  Full startup command line\n");
    printf("  TB_MANAGED_TERMINAL_SHELL    Shell executable fallback\n");
}

int main(int argc, char **argv) {
    TbManagedTerminalOptions options;
    TbManagedTerminalStatus status;
    TbManagedTerminal *terminal = NULL;
    char *snapshot = NULL;
    size_t snapshot_len = 0;
    const char *title = NULL;
    const char *send_text = NULL;
    DWORD pump_ms = 1200;
    DWORD started_at = 0;

    tb_managed_terminal_options_defaults(&options);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--pump-ms") == 0 && i + 1 < argc) {
            pump_ms = (DWORD) strtoul(argv[++i], NULL, 10);
            continue;
        }
        if (strcmp(argv[i], "--send") == 0 && i + 1 < argc) {
            send_text = argv[++i];
            continue;
        }
    }

    terminal = tb_managed_terminal_create(&options);
    if (terminal == NULL) {
        fprintf(stderr, "managed-terminal-probe: failed to create terminal\n");
        return 1;
    }

#ifdef _WIN32
    started_at = GetTickCount();
    while ((GetTickCount() - started_at) < pump_ms) {
        bool had_output = false;
        if (!tb_managed_terminal_pump_output(terminal, &had_output)) {
            break;
        }
        if (had_output && send_text != NULL) {
            tb_managed_terminal_write_text(terminal, send_text);
            tb_managed_terminal_write_text(terminal, "\r\n");
            send_text = NULL;
        }
        Sleep(25);
    }
#else
    (void) started_at;
#endif

    tb_managed_terminal_fill_status(terminal, &status);
    if (tb_managed_terminal_take_title_update(terminal, &title) && title != NULL && title[0] != '\0') {
        printf("title: %s\n", title);
    }

    printf(
        "alive=%s exited=%s pending=%s exit_code=%d size=%ux%u\n",
        status.child_alive ? "true" : "false",
        status.child_exited ? "true" : "false",
        status.has_pending_output ? "true" : "false",
        status.exit_code,
        (unsigned int) status.cols,
        (unsigned int) status.rows
    );

    if (tb_managed_terminal_snapshot_text_alloc(terminal, &snapshot, &snapshot_len)) {
        printf("--- snapshot (%zu bytes) ---\n", snapshot_len);
        printf("%s\n", snapshot);
    } else {
        fprintf(stderr, "managed-terminal-probe: failed to snapshot terminal text\n");
    }

    free(snapshot);
    tb_managed_terminal_destroy(terminal);
    return 0;
}
