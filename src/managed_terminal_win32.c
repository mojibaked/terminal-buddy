#include "managed_terminal.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghostty/vt.h>

typedef struct TbConPty {
    HPCON hpc;
    HANDLE pipe_in;
    HANDLE pipe_out;
    HANDLE process;
    HANDLE thread;
    uint16_t cols;
    uint16_t rows;
} TbConPty;

typedef enum TbConPtyReadResult {
    TB_CONPTY_READ_OK = 0,
    TB_CONPTY_READ_EOF,
    TB_CONPTY_READ_ERROR,
} TbConPtyReadResult;

struct TbManagedTerminal {
    TbConPty *pty;
    GhosttyTerminal terminal;
    bool child_exited;
    bool title_dirty;
    uint16_t cols;
    uint16_t rows;
    uint32_t cell_width;
    uint32_t cell_height;
    char title[TB_MANAGED_TERMINAL_TITLE_MAX];
};

static bool tb_env_to_buffer(const wchar_t *name, wchar_t *out_text, size_t out_text_len) {
    DWORD value_len = 0;

    if (name == NULL || out_text == NULL || out_text_len == 0) {
        return false;
    }

    value_len = GetEnvironmentVariableW(name, out_text, (DWORD) out_text_len);
    if (value_len == 0 || value_len >= out_text_len) {
        return false;
    }
    return true;
}

static bool tb_find_shell(wchar_t *out_text, size_t out_text_len) {
    static const wchar_t *const candidates[] = {
        L"powershell.exe",
        L"pwsh.exe",
        L"cmd.exe",
    };
    size_t i = 0;

    if (out_text == NULL || out_text_len == 0) {
        return false;
    }

    if (tb_env_to_buffer(L"TB_MANAGED_TERMINAL_SHELL", out_text, out_text_len)) {
        return true;
    }

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        DWORD found = SearchPathW(NULL, candidates[i], NULL, (DWORD) out_text_len, out_text, NULL);
        if (found > 0 && found < out_text_len) {
            return true;
        }
    }

    return wcscpy_s(out_text, out_text_len, L"cmd.exe") == 0;
}

static bool tb_build_startup_command(
    const TbManagedTerminalOptions *options,
    wchar_t *out_text,
    size_t out_text_len
) {
    wchar_t shell[MAX_PATH];

    if (out_text == NULL || out_text_len == 0) {
        return false;
    }

    if (options != NULL && options->startup_command != NULL && options->startup_command[0] != L'\0') {
        return wcsncpy_s(out_text, out_text_len, options->startup_command, _TRUNCATE) == 0;
    }

    if (tb_env_to_buffer(L"TB_MANAGED_TERMINAL_COMMAND", out_text, out_text_len)) {
        return true;
    }

    if (!tb_find_shell(shell, sizeof(shell) / sizeof(shell[0]))) {
        return false;
    }

    return swprintf(out_text, out_text_len, L"\"%ls\"", shell) >= 0;
}

static bool tb_conpty_create(TbConPty **out_pty, uint16_t cols, uint16_t rows, const wchar_t *startup_command) {
    TbConPty *pty = NULL;
    HANDLE pipe_in_read = INVALID_HANDLE_VALUE;
    HANDLE pipe_in_write = INVALID_HANDLE_VALUE;
    HANDLE pipe_out_read = INVALID_HANDLE_VALUE;
    HANDLE pipe_out_write = INVALID_HANDLE_VALUE;
    HPCON hpc = INVALID_HANDLE_VALUE;
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = NULL;
    SIZE_T attr_size = 0;
    bool attr_list_initialized = false;
    STARTUPINFOEXW startup_info;
    PROCESS_INFORMATION process_info;
    COORD size;
    HRESULT hr = S_OK;
    DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT;
    wchar_t command_line[4096];

    if (out_pty != NULL) {
        *out_pty = NULL;
    }
    if (out_pty == NULL || startup_command == NULL || startup_command[0] == L'\0') {
        return false;
    }

    pty = (TbConPty *) calloc(1, sizeof(*pty));
    if (pty == NULL) {
        return false;
    }

    if (!CreatePipe(&pipe_in_read, &pipe_in_write, NULL, 0)) {
        goto fail;
    }
    if (!CreatePipe(&pipe_out_read, &pipe_out_write, NULL, 0)) {
        goto fail;
    }

    size.X = (SHORT) cols;
    size.Y = (SHORT) rows;
    hr = CreatePseudoConsole(size, pipe_in_read, pipe_out_write, 0, &hpc);
    if (FAILED(hr)) {
        goto fail;
    }

    CloseHandle(pipe_in_read);
    pipe_in_read = INVALID_HANDLE_VALUE;
    CloseHandle(pipe_out_write);
    pipe_out_write = INVALID_HANDLE_VALUE;

    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST) malloc(attr_size);
    if (attr_list == NULL) {
        goto fail;
    }
    if (!InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size)) {
        goto fail;
    }
    attr_list_initialized = true;
    if (!UpdateProcThreadAttribute(
            attr_list,
            0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
            hpc,
            sizeof(HPCON),
            NULL,
            NULL
        )) {
        goto fail;
    }

    if (wcsncpy_s(command_line, sizeof(command_line) / sizeof(command_line[0]), startup_command, _TRUNCATE) != 0) {
        goto fail;
    }

    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.StartupInfo.cb = sizeof(startup_info);
    startup_info.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startup_info.StartupInfo.hStdInput = NULL;
    startup_info.StartupInfo.hStdOutput = NULL;
    startup_info.StartupInfo.hStdError = NULL;
    startup_info.lpAttributeList = attr_list;

    ZeroMemory(&process_info, sizeof(process_info));
    SetEnvironmentVariableA("TERM", "xterm-256color");

    if (!CreateProcessW(
            NULL,
            command_line,
            NULL,
            NULL,
            FALSE,
            creation_flags,
            NULL,
            NULL,
            &startup_info.StartupInfo,
            &process_info
        )) {
        goto fail;
    }

    DeleteProcThreadAttributeList(attr_list);
    free(attr_list);
    attr_list = NULL;
    attr_list_initialized = false;

    pty->hpc = hpc;
    pty->pipe_in = pipe_in_write;
    pty->pipe_out = pipe_out_read;
    pty->process = process_info.hProcess;
    pty->thread = process_info.hThread;
    pty->cols = cols;
    pty->rows = rows;
    *out_pty = pty;
    return true;

fail:
    if (attr_list_initialized) {
        DeleteProcThreadAttributeList(attr_list);
    }
    free(attr_list);
    if (hpc != INVALID_HANDLE_VALUE) {
        ClosePseudoConsole(hpc);
    }
    if (pipe_in_read != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_in_read);
    }
    if (pipe_in_write != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_in_write);
    }
    if (pipe_out_read != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_out_read);
    }
    if (pipe_out_write != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_out_write);
    }
    free(pty);
    return false;
}

static void tb_conpty_destroy(TbConPty *pty) {
    if (pty == NULL) {
        return;
    }

    if (pty->pipe_in != INVALID_HANDLE_VALUE) {
        CloseHandle(pty->pipe_in);
    }
    if (pty->pipe_out != INVALID_HANDLE_VALUE) {
        CloseHandle(pty->pipe_out);
    }
    if (pty->thread != NULL) {
        CloseHandle(pty->thread);
    }
    if (pty->process != NULL) {
        CloseHandle(pty->process);
    }
    if (pty->hpc != INVALID_HANDLE_VALUE) {
        ClosePseudoConsole(pty->hpc);
    }
    free(pty);
}

static bool tb_conpty_has_pending_output(const TbConPty *pty) {
    DWORD available = 0;

    if (pty == NULL || pty->pipe_out == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (!PeekNamedPipe(pty->pipe_out, NULL, 0, NULL, &available, NULL)) {
        return false;
    }
    return available > 0;
}

static bool tb_conpty_write(TbConPty *pty, const char *buffer, size_t length) {
    if (pty == NULL || pty->pipe_in == INVALID_HANDLE_VALUE || buffer == NULL || length == 0) {
        return false;
    }

    while (length > 0) {
        DWORD written = 0;
        if (!WriteFile(pty->pipe_in, buffer, (DWORD) length, &written, NULL)) {
            return false;
        }
        buffer += written;
        length -= written;
    }
    return true;
}

static TbConPtyReadResult tb_conpty_read(TbConPty *pty, GhosttyTerminal terminal, bool *out_had_output) {
    uint8_t buffer[4096];
    DWORD available = 0;
    DWORD to_read = 0;
    DWORD bytes_read = 0;

    if (out_had_output != NULL) {
        *out_had_output = false;
    }
    if (pty == NULL || terminal == NULL) {
        return TB_CONPTY_READ_ERROR;
    }

    while (true) {
        if (!PeekNamedPipe(pty->pipe_out, NULL, 0, NULL, &available, NULL)) {
            DWORD error = GetLastError();
            return error == ERROR_BROKEN_PIPE ? TB_CONPTY_READ_EOF : TB_CONPTY_READ_ERROR;
        }
        if (available == 0) {
            return TB_CONPTY_READ_OK;
        }

        to_read = available < sizeof(buffer) ? available : (DWORD) sizeof(buffer);
        if (!ReadFile(pty->pipe_out, buffer, to_read, &bytes_read, NULL)) {
            DWORD error = GetLastError();
            return error == ERROR_BROKEN_PIPE ? TB_CONPTY_READ_EOF : TB_CONPTY_READ_ERROR;
        }
        if (bytes_read == 0) {
            return TB_CONPTY_READ_EOF;
        }

        ghostty_terminal_vt_write(terminal, buffer, (size_t) bytes_read);
        if (out_had_output != NULL) {
            *out_had_output = true;
        }

        if (bytes_read < to_read) {
            return TB_CONPTY_READ_OK;
        }
    }
}

static void tb_conpty_resize(TbConPty *pty, uint16_t cols, uint16_t rows) {
    COORD size;

    if (pty == NULL || pty->hpc == INVALID_HANDLE_VALUE) {
        return;
    }

    size.X = (SHORT) cols;
    size.Y = (SHORT) rows;
    ResizePseudoConsole(pty->hpc, size);
    pty->cols = cols;
    pty->rows = rows;
}

static bool tb_conpty_is_alive(const TbConPty *pty) {
    if (pty == NULL || pty->process == NULL) {
        return false;
    }
    return WaitForSingleObject(pty->process, 0) == WAIT_TIMEOUT;
}

static int tb_conpty_exit_code(const TbConPty *pty) {
    DWORD exit_code = STILL_ACTIVE;

    if (pty == NULL || pty->process == NULL) {
        return -1;
    }
    if (!GetExitCodeProcess(pty->process, &exit_code)) {
        return -1;
    }
    return exit_code == STILL_ACTIVE ? -1 : (int) exit_code;
}

static void tb_terminal_effect_write_pty(GhosttyTerminal terminal, void *userdata, const uint8_t *data, size_t len) {
    TbManagedTerminal *managed = (TbManagedTerminal *) userdata;

    (void) terminal;
    if (managed == NULL || managed->pty == NULL || data == NULL || len == 0) {
        return;
    }
    tb_conpty_write(managed->pty, (const char *) data, len);
}

static bool tb_terminal_effect_size(GhosttyTerminal terminal, void *userdata, GhosttySizeReportSize *out_size) {
    TbManagedTerminal *managed = (TbManagedTerminal *) userdata;

    (void) terminal;
    if (managed == NULL || out_size == NULL) {
        return false;
    }

    memset(out_size, 0, sizeof(*out_size));
    out_size->rows = managed->rows;
    out_size->columns = managed->cols;
    out_size->cell_width = managed->cell_width;
    out_size->cell_height = managed->cell_height;
    return true;
}

static bool tb_terminal_effect_device_attributes(
    GhosttyTerminal terminal,
    void *userdata,
    GhosttyDeviceAttributes *out_attrs
) {
    (void) terminal;
    (void) userdata;

    if (out_attrs == NULL) {
        return false;
    }

    out_attrs->primary.conformance_level = GHOSTTY_DA_CONFORMANCE_VT220;
    out_attrs->primary.features[0] = GHOSTTY_DA_FEATURE_COLUMNS_132;
    out_attrs->primary.features[1] = GHOSTTY_DA_FEATURE_SELECTIVE_ERASE;
    out_attrs->primary.features[2] = GHOSTTY_DA_FEATURE_ANSI_COLOR;
    out_attrs->primary.num_features = 3;
    out_attrs->secondary.device_type = GHOSTTY_DA_DEVICE_TYPE_VT220;
    out_attrs->secondary.firmware_version = 1;
    out_attrs->secondary.rom_cartridge = 0;
    out_attrs->tertiary.unit_id = 0;
    return true;
}

static GhosttyString tb_terminal_effect_xtversion(GhosttyTerminal terminal, void *userdata) {
    (void) terminal;
    (void) userdata;

    return (GhosttyString) {
        .ptr = (const uint8_t *) "terminal-buddy",
        .len = 14,
    };
}

static void tb_terminal_effect_title_changed(GhosttyTerminal terminal, void *userdata) {
    TbManagedTerminal *managed = (TbManagedTerminal *) userdata;
    GhosttyString title = {0};
    size_t copy_len = 0;

    if (managed == NULL) {
        return;
    }
    if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_TITLE, &title) != GHOSTTY_SUCCESS) {
        return;
    }

    copy_len = title.len < (TB_MANAGED_TERMINAL_TITLE_MAX - 1)
        ? title.len
        : (TB_MANAGED_TERMINAL_TITLE_MAX - 1);
    memcpy(managed->title, title.ptr, copy_len);
    managed->title[copy_len] = '\0';
    managed->title_dirty = true;
}

static bool tb_terminal_effect_color_scheme(
    GhosttyTerminal terminal,
    void *userdata,
    GhosttyColorScheme *out_scheme
) {
    (void) terminal;
    (void) userdata;
    (void) out_scheme;
    return false;
}

void tb_managed_terminal_options_defaults(TbManagedTerminalOptions *options) {
    if (options == NULL) {
        return;
    }

    memset(options, 0, sizeof(*options));
    options->cols = 120;
    options->rows = 36;
    options->cell_width = 8;
    options->cell_height = 16;
    options->max_scrollback = 1000;
}

TbManagedTerminal *tb_managed_terminal_create(const TbManagedTerminalOptions *options) {
    TbManagedTerminalOptions resolved;
    TbManagedTerminal *managed = NULL;
    GhosttyTerminalOptions terminal_options = {0};
    GhosttyResult result = GHOSTTY_SUCCESS;
    wchar_t startup_command[4096];

    tb_managed_terminal_options_defaults(&resolved);
    if (options != NULL) {
        resolved = *options;
        if (resolved.cols == 0) {
            resolved.cols = 120;
        }
        if (resolved.rows == 0) {
            resolved.rows = 36;
        }
        if (resolved.cell_width == 0) {
            resolved.cell_width = 8;
        }
        if (resolved.cell_height == 0) {
            resolved.cell_height = 16;
        }
        if (resolved.max_scrollback == 0) {
            resolved.max_scrollback = 1000;
        }
    }

    if (!tb_build_startup_command(options, startup_command, sizeof(startup_command) / sizeof(startup_command[0]))) {
        return NULL;
    }

    managed = (TbManagedTerminal *) calloc(1, sizeof(*managed));
    if (managed == NULL) {
        return NULL;
    }

    managed->cols = resolved.cols;
    managed->rows = resolved.rows;
    managed->cell_width = resolved.cell_width;
    managed->cell_height = resolved.cell_height;

    terminal_options.cols = resolved.cols;
    terminal_options.rows = resolved.rows;
    terminal_options.max_scrollback = resolved.max_scrollback;
    result = ghostty_terminal_new(NULL, &managed->terminal, terminal_options);
    if (result != GHOSTTY_SUCCESS) {
        tb_managed_terminal_destroy(managed);
        return NULL;
    }

    ghostty_terminal_resize(
        managed->terminal,
        resolved.cols,
        resolved.rows,
        resolved.cell_width,
        resolved.cell_height
    );

    ghostty_terminal_set(managed->terminal, GHOSTTY_TERMINAL_OPT_USERDATA, managed);
    ghostty_terminal_set(managed->terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY, (const void *) tb_terminal_effect_write_pty);
    ghostty_terminal_set(managed->terminal, GHOSTTY_TERMINAL_OPT_SIZE, (const void *) tb_terminal_effect_size);
    ghostty_terminal_set(
        managed->terminal,
        GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES,
        (const void *) tb_terminal_effect_device_attributes
    );
    ghostty_terminal_set(managed->terminal, GHOSTTY_TERMINAL_OPT_XTVERSION, (const void *) tb_terminal_effect_xtversion);
    ghostty_terminal_set(
        managed->terminal,
        GHOSTTY_TERMINAL_OPT_TITLE_CHANGED,
        (const void *) tb_terminal_effect_title_changed
    );
    ghostty_terminal_set(
        managed->terminal,
        GHOSTTY_TERMINAL_OPT_COLOR_SCHEME,
        (const void *) tb_terminal_effect_color_scheme
    );

    if (!tb_conpty_create(&managed->pty, resolved.cols, resolved.rows, startup_command)) {
        tb_managed_terminal_destroy(managed);
        return NULL;
    }

    return managed;
}

void tb_managed_terminal_destroy(TbManagedTerminal *terminal) {
    if (terminal == NULL) {
        return;
    }

    if (terminal->terminal != NULL) {
        ghostty_terminal_free(terminal->terminal);
    }
    tb_conpty_destroy(terminal->pty);
    free(terminal);
}

bool tb_managed_terminal_resize(
    TbManagedTerminal *terminal,
    uint16_t cols,
    uint16_t rows,
    uint32_t cell_width,
    uint32_t cell_height
) {
    if (terminal == NULL || terminal->terminal == NULL || cols == 0 || rows == 0 || cell_width == 0 || cell_height == 0) {
        return false;
    }

    terminal->cols = cols;
    terminal->rows = rows;
    terminal->cell_width = cell_width;
    terminal->cell_height = cell_height;
    ghostty_terminal_resize(terminal->terminal, cols, rows, cell_width, cell_height);
    if (!terminal->child_exited) {
        tb_conpty_resize(terminal->pty, cols, rows);
    }
    return true;
}

bool tb_managed_terminal_pump_output(TbManagedTerminal *terminal, bool *out_had_output) {
    TbConPtyReadResult read_result = TB_CONPTY_READ_OK;

    if (out_had_output != NULL) {
        *out_had_output = false;
    }
    if (terminal == NULL || terminal->terminal == NULL || terminal->pty == NULL) {
        return false;
    }
    if (terminal->child_exited) {
        return true;
    }

    read_result = tb_conpty_read(terminal->pty, terminal->terminal, out_had_output);
    if (read_result != TB_CONPTY_READ_OK) {
        terminal->child_exited = true;
    } else if (!tb_conpty_is_alive(terminal->pty)) {
        terminal->child_exited = true;
    }

    return read_result != TB_CONPTY_READ_ERROR;
}

bool tb_managed_terminal_has_pending_output(const TbManagedTerminal *terminal) {
    return terminal != NULL && terminal->pty != NULL && tb_conpty_has_pending_output(terminal->pty);
}

bool tb_managed_terminal_write(TbManagedTerminal *terminal, const char *text, size_t len) {
    if (terminal == NULL || terminal->pty == NULL || terminal->child_exited || text == NULL || len == 0) {
        return false;
    }
    return tb_conpty_write(terminal->pty, text, len);
}

bool tb_managed_terminal_write_text(TbManagedTerminal *terminal, const char *text) {
    return text != NULL && tb_managed_terminal_write(terminal, text, strlen(text));
}

bool tb_managed_terminal_snapshot_text_alloc(
    TbManagedTerminal *terminal,
    char **out_text,
    size_t *out_len
) {
    GhosttyFormatter formatter = NULL;
    GhosttyFormatterTerminalOptions options = GHOSTTY_INIT_SIZED(GhosttyFormatterTerminalOptions);
    uint8_t *buffer = NULL;
    size_t length = 0;
    char *text = NULL;
    GhosttyResult result = GHOSTTY_SUCCESS;

    if (out_text != NULL) {
        *out_text = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (terminal == NULL || terminal->terminal == NULL || out_text == NULL) {
        return false;
    }

    options.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    options.unwrap = true;
    options.trim = true;

    result = ghostty_formatter_terminal_new(NULL, &formatter, terminal->terminal, options);
    if (result != GHOSTTY_SUCCESS) {
        return false;
    }

    result = ghostty_formatter_format_alloc(formatter, NULL, &buffer, &length);
    ghostty_formatter_free(formatter);
    if (result != GHOSTTY_SUCCESS) {
        return false;
    }

    text = (char *) malloc(length + 1);
    if (text == NULL) {
        ghostty_free(NULL, buffer, length);
        return false;
    }

    if (length > 0) {
        memcpy(text, buffer, length);
    }
    text[length] = '\0';
    ghostty_free(NULL, buffer, length);

    *out_text = text;
    if (out_len != NULL) {
        *out_len = length;
    }
    return true;
}

bool tb_managed_terminal_take_title_update(TbManagedTerminal *terminal, const char **out_title) {
    if (out_title != NULL) {
        *out_title = NULL;
    }
    if (terminal == NULL || !terminal->title_dirty) {
        return false;
    }

    if (out_title != NULL) {
        *out_title = terminal->title;
    }
    terminal->title_dirty = false;
    return true;
}

void tb_managed_terminal_fill_status(const TbManagedTerminal *terminal, TbManagedTerminalStatus *out_status) {
    if (out_status == NULL) {
        return;
    }

    memset(out_status, 0, sizeof(*out_status));
    out_status->exit_code = -1;
    if (terminal == NULL) {
        return;
    }

    out_status->child_exited = terminal->child_exited;
    out_status->child_alive = terminal->pty != NULL && !terminal->child_exited && tb_conpty_is_alive(terminal->pty);
    out_status->has_pending_output = terminal->pty != NULL && tb_conpty_has_pending_output(terminal->pty);
    out_status->exit_code = terminal->pty != NULL ? tb_conpty_exit_code(terminal->pty) : -1;
    out_status->cols = terminal->cols;
    out_status->rows = terminal->rows;
    out_status->cell_width = terminal->cell_width;
    out_status->cell_height = terminal->cell_height;
    memcpy(out_status->title, terminal->title, sizeof(out_status->title));
}

#else

#include <string.h>

void tb_managed_terminal_options_defaults(TbManagedTerminalOptions *options) {
    if (options != NULL) {
        memset(options, 0, sizeof(*options));
    }
}

TbManagedTerminal *tb_managed_terminal_create(const TbManagedTerminalOptions *options) {
    (void) options;
    return NULL;
}

void tb_managed_terminal_destroy(TbManagedTerminal *terminal) {
    (void) terminal;
}

bool tb_managed_terminal_resize(
    TbManagedTerminal *terminal,
    uint16_t cols,
    uint16_t rows,
    uint32_t cell_width,
    uint32_t cell_height
) {
    (void) terminal;
    (void) cols;
    (void) rows;
    (void) cell_width;
    (void) cell_height;
    return false;
}

bool tb_managed_terminal_pump_output(TbManagedTerminal *terminal, bool *out_had_output) {
    (void) terminal;
    if (out_had_output != NULL) {
        *out_had_output = false;
    }
    return false;
}

bool tb_managed_terminal_has_pending_output(const TbManagedTerminal *terminal) {
    (void) terminal;
    return false;
}

bool tb_managed_terminal_write(TbManagedTerminal *terminal, const char *text, size_t len) {
    (void) terminal;
    (void) text;
    (void) len;
    return false;
}

bool tb_managed_terminal_write_text(TbManagedTerminal *terminal, const char *text) {
    (void) terminal;
    (void) text;
    return false;
}

bool tb_managed_terminal_snapshot_text_alloc(
    TbManagedTerminal *terminal,
    char **out_text,
    size_t *out_len
) {
    (void) terminal;
    if (out_text != NULL) {
        *out_text = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    return false;
}

bool tb_managed_terminal_take_title_update(TbManagedTerminal *terminal, const char **out_title) {
    (void) terminal;
    if (out_title != NULL) {
        *out_title = NULL;
    }
    return false;
}

void tb_managed_terminal_fill_status(const TbManagedTerminal *terminal, TbManagedTerminalStatus *out_status) {
    (void) terminal;
    if (out_status != NULL) {
        memset(out_status, 0, sizeof(*out_status));
        out_status->exit_code = -1;
    }
}

#endif
