#include "sidecar_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef _WIN32
bool tb_sidecar_client_upsert_window_target(
    TbSidecarClient *client,
    const char *target_kind,
    const char *provider,
    const char *target_id,
    const char *project_id,
    const char *cwd,
    const char *label,
    const char *status,
    const char *attention,
    const char *window_id,
    Uint32 process_id,
    const char *process_name,
    const char *occupancy,
    const char *attached_agent_target_id,
    const char *host_shell_target_id
);
#endif

static bool env_truthy(const char *value) {
    return value != NULL
        && (SDL_strcmp(value, "1") == 0 || SDL_strcasecmp(value, "true") == 0 || SDL_strcasecmp(value, "yes") == 0);
}

static void sidecar_set_detail(TbSidecarClient *client, const char *text) {
    if (client == NULL || client->mutex == NULL) {
        return;
    }

    SDL_LockMutex(client->mutex);
    SDL_snprintf(client->detail_text, sizeof(client->detail_text), "%s", text != NULL ? text : "");
    SDL_UnlockMutex(client->mutex);
}

static void sidecar_set_error(TbSidecarClient *client, const char *text) {
    if (client == NULL || client->mutex == NULL) {
        return;
    }

    SDL_LockMutex(client->mutex);
    SDL_snprintf(client->error_text, sizeof(client->error_text), "%s", text != NULL ? text : "");
    SDL_UnlockMutex(client->mutex);
}

static void sidecar_set_ready(TbSidecarClient *client, bool ready) {
    if (client == NULL || client->mutex == NULL) {
        return;
    }

    SDL_LockMutex(client->mutex);
    client->ready = ready;
    if (ready) {
        client->error_text[0] = '\0';
    }
    SDL_UnlockMutex(client->mutex);
}

static void sidecar_wake_route_waiters(TbSidecarClient *client) {
    if (client == NULL || client->mutex == NULL || client->response_condition == NULL) {
        return;
    }

    SDL_SignalCondition(client->response_condition);
}

static char *json_escape(const char *text) {
    size_t length = 0;
    size_t capacity = 0;
    char *escaped = NULL;
    const char *cursor = NULL;

    if (text == NULL) {
        return SDL_strdup("");
    }

    capacity = (SDL_strlen(text) * 2u) + 1u;
    escaped = (char *) SDL_malloc(capacity);
    if (escaped == NULL) {
        return NULL;
    }

    cursor = text;
    while (*cursor != '\0') {
        if (length + 3 >= capacity) {
            char *grown = (char *) SDL_realloc(escaped, capacity * 2u);
            if (grown == NULL) {
                SDL_free(escaped);
                return NULL;
            }
            escaped = grown;
            capacity *= 2u;
        }

        switch (*cursor) {
            case '\\':
                escaped[length++] = '\\';
                escaped[length++] = '\\';
                break;
            case '"':
                escaped[length++] = '\\';
                escaped[length++] = '"';
                break;
            case '\n':
                escaped[length++] = '\\';
                escaped[length++] = 'n';
                break;
            case '\r':
                escaped[length++] = '\\';
                escaped[length++] = 'r';
                break;
            case '\t':
                escaped[length++] = '\\';
                escaped[length++] = 't';
                break;
            default:
                escaped[length++] = *cursor;
                break;
        }
        ++cursor;
    }

    escaped[length] = '\0';
    return escaped;
}

static void format_timestamp_iso8601(char *out_text, size_t out_text_size) {
    time_t raw_time = 0;
    struct tm utc_time;

    if (out_text == NULL || out_text_size == 0) {
        return;
    }

    out_text[0] = '\0';
    time(&raw_time);
#ifdef _WIN32
    gmtime_s(&utc_time, &raw_time);
#else
    gmtime_r(&raw_time, &utc_time);
#endif
    strftime(out_text, out_text_size, "%Y-%m-%dT%H:%M:%SZ", &utc_time);
}

static bool line_extract_string(const char *line, const char *key, char *out_text, size_t out_text_size) {
    const char *found = NULL;
    const char *cursor = NULL;
    size_t written = 0;

    if (line == NULL || key == NULL || out_text == NULL || out_text_size == 0) {
        return false;
    }

    out_text[0] = '\0';
    found = SDL_strstr(line, key);
    if (found == NULL) {
        return false;
    }

    cursor = found + SDL_strlen(key);
    while (*cursor != '\0' && written + 1 < out_text_size) {
        if (*cursor == '"' && cursor > found && cursor[-1] != '\\') {
            break;
        }
        out_text[written++] = *cursor++;
    }
    out_text[written] = '\0';
    return written > 0;
}

static bool line_extract_bool(const char *line, const char *key, bool *out_value) {
    const char *found = NULL;
    const char *cursor = NULL;

    if (line == NULL || key == NULL || out_value == NULL) {
        return false;
    }

    found = SDL_strstr(line, key);
    if (found == NULL) {
        return false;
    }

    cursor = found + SDL_strlen(key);
    if (SDL_strncmp(cursor, "true", 4) == 0) {
        *out_value = true;
        return true;
    }
    if (SDL_strncmp(cursor, "false", 5) == 0) {
        *out_value = false;
        return true;
    }
    return false;
}

static void sidecar_reset_route_response(TbSidecarClient *client) {
    if (client == NULL) {
        return;
    }

    SDL_zero(client->route_response);
    client->pending_utterance_id[0] = '\0';
    client->route_response_ready = false;
    client->awaiting_route_response = false;
}

static void sidecar_reset_project_summary_response(TbSidecarClient *client) {
    if (client == NULL) {
        return;
    }

    SDL_zero(client->project_summary_response);
    client->pending_project_id[0] = '\0';
    client->project_summary_response_ready = false;
    client->awaiting_project_summary_response = false;
}

static void sidecar_reset_target_snapshot_response(TbSidecarClient *client) {
    if (client == NULL) {
        return;
    }

    SDL_zero(client->target_snapshot_response);
    client->pending_target_id[0] = '\0';
    client->target_snapshot_response_ready = false;
    client->awaiting_target_snapshot_response = false;
}

static void sidecar_store_route_response(TbSidecarClient *client, const TbSidecarRouteDecision *decision) {
    if (client == NULL || decision == NULL || client->mutex == NULL) {
        return;
    }

    SDL_LockMutex(client->mutex);
    if (
        client->awaiting_route_response
        && client->pending_utterance_id[0] != '\0'
        && SDL_strcmp(client->pending_utterance_id, decision->utterance_id) == 0
    ) {
        client->route_response = *decision;
        client->route_response.received = true;
        client->route_response_ready = true;
        client->awaiting_route_response = false;
        sidecar_wake_route_waiters(client);
    }
    SDL_UnlockMutex(client->mutex);
}

static bool sidecar_matches_pending_id(const char *pending_id, const char *response_id) {
    if (pending_id == NULL || response_id == NULL) {
        return false;
    }

    if (pending_id[0] == '\0' && response_id[0] == '\0') {
        return true;
    }

    return SDL_strcmp(pending_id, response_id) == 0;
}

static void sidecar_store_project_summary_response(TbSidecarClient *client, const TbSidecarProjectSummary *summary) {
    if (client == NULL || summary == NULL || client->mutex == NULL) {
        return;
    }

    SDL_LockMutex(client->mutex);
    if (
        client->awaiting_project_summary_response
        && sidecar_matches_pending_id(client->pending_project_id, summary->project_id)
    ) {
        client->project_summary_response = *summary;
        client->project_summary_response.received = true;
        client->project_summary_response_ready = true;
        client->awaiting_project_summary_response = false;
        sidecar_wake_route_waiters(client);
    }
    SDL_UnlockMutex(client->mutex);
}

static void sidecar_store_target_snapshot_response(TbSidecarClient *client, const TbSidecarTargetSnapshot *snapshot) {
    if (client == NULL || snapshot == NULL || client->mutex == NULL) {
        return;
    }

    SDL_LockMutex(client->mutex);
    if (
        client->awaiting_target_snapshot_response
        && sidecar_matches_pending_id(client->pending_target_id, snapshot->target_id)
    ) {
        client->target_snapshot_response = *snapshot;
        client->target_snapshot_response.received = true;
        client->target_snapshot_response_ready = true;
        client->awaiting_target_snapshot_response = false;
        sidecar_wake_route_waiters(client);
    }
    SDL_UnlockMutex(client->mutex);
}

static void sidecar_cancel_pending_requests(TbSidecarClient *client) {
    if (client == NULL || client->mutex == NULL) {
        return;
    }

    SDL_LockMutex(client->mutex);
    sidecar_reset_route_response(client);
    sidecar_reset_project_summary_response(client);
    sidecar_reset_target_snapshot_response(client);
    sidecar_wake_route_waiters(client);
    SDL_UnlockMutex(client->mutex);
}

static const char *snapshot_activity_phrase(const char *activity_kind) {
    if (activity_kind == NULL) {
        return "is active";
    }

    if (SDL_strcmp(activity_kind, "agent_session") == 0) {
        return "is in an agent session";
    }
    if (SDL_strcmp(activity_kind, "shell_prompt") == 0) {
        return "is at a shell prompt";
    }
    if (SDL_strcmp(activity_kind, "running_job") == 0) {
        return "is running work";
    }
    if (SDL_strcmp(activity_kind, "project_overview") == 0) {
        return "is acting as a project overview";
    }
    return "is active";
}

#ifdef _WIN32
static HANDLE tb_handle(void *value) {
    return (HANDLE) value;
}

static bool sidecar_write_line(TbSidecarClient *client, const char *line) {
    DWORD written = 0;
    size_t line_length = 0;
    char *buffer = NULL;
    bool ok = false;

    if (client == NULL || client->stdin_write == NULL || line == NULL) {
        return false;
    }

    line_length = SDL_strlen(line);
    buffer = (char *) SDL_malloc(line_length + 2u);
    if (buffer == NULL) {
        return false;
    }

    SDL_memcpy(buffer, line, line_length);
    buffer[line_length] = '\n';
    buffer[line_length + 1u] = '\0';

    ok = WriteFile(tb_handle(client->stdin_write), buffer, (DWORD) (line_length + 1u), &written, NULL) && written == (DWORD) (line_length + 1u);
    SDL_free(buffer);
    return ok;
}

static void sidecar_process_line(TbSidecarClient *client, const char *line) {
    TbSidecarRouteDecision route_decision;
    TbSidecarProjectSummary project_summary;
    TbSidecarTargetSnapshot target_snapshot;
    char category[64];
    char reason[TB_SIDECAR_TEXT_MAX];
    char target_label[TB_SIDECAR_TEXT_MAX];
    char target_id[TB_SIDECAR_ID_MAX];
    char project_id[TB_SIDECAR_ID_MAX];
    char utterance_id[64];
    char host_action_kind[TB_SIDECAR_CATEGORY_MAX];
    char agent_provider[TB_SIDECAR_CATEGORY_MAX];
    char command_text[TB_SIDECAR_COMMAND_MAX];
    char activity_kind[TB_SIDECAR_CATEGORY_MAX];
    char summary_text[TB_SIDECAR_SUMMARY_MAX];
    char recent_text[TB_SIDECAR_SUMMARY_MAX];
    char title[TB_SIDECAR_TEXT_MAX];
    char message[TB_SIDECAR_TEXT_MAX];

    if (client == NULL || line == NULL) {
        return;
    }

    if (SDL_strstr(line, "\"kind\":\"handshake_response\"") != NULL) {
        if (SDL_strstr(line, "\"accepted\":true") != NULL) {
            sidecar_set_ready(client, true);
            sidecar_set_detail(client, "sidecar: ready");
        } else {
            sidecar_set_error(client, "sidecar rejected the handshake.");
        }
        return;
    }

    if (SDL_strstr(line, "\"kind\":\"voice_utterance_response\"") != NULL) {
        SDL_zero(route_decision);
        category[0] = '\0';
        reason[0] = '\0';
        target_label[0] = '\0';
        target_id[0] = '\0';
        project_id[0] = '\0';
        utterance_id[0] = '\0';
        host_action_kind[0] = '\0';
        agent_provider[0] = '\0';
        command_text[0] = '\0';
        route_decision.submit = false;
        line_extract_string(line, "\"utteranceId\":\"", utterance_id, sizeof(utterance_id));
        line_extract_string(line, "\"category\":\"", category, sizeof(category));
        line_extract_string(line, "\"hostAction\":{\"kind\":\"", host_action_kind, sizeof(host_action_kind));
        line_extract_string(line, "\"agentProvider\":\"", agent_provider, sizeof(agent_provider));
        line_extract_string(line, "\"reason\":\"", reason, sizeof(reason));
        line_extract_string(line, "\"selectedTargetLabel\":\"", target_label, sizeof(target_label));
        line_extract_string(line, "\"selectedTargetId\":\"", target_id, sizeof(target_id));
        line_extract_string(line, "\"selectedProjectId\":\"", project_id, sizeof(project_id));
        line_extract_string(line, "\"commandText\":\"", command_text, sizeof(command_text));
        line_extract_bool(line, "\"submit\":", &route_decision.submit);

        SDL_snprintf(route_decision.utterance_id, sizeof(route_decision.utterance_id), "%s", utterance_id);
        SDL_snprintf(route_decision.category, sizeof(route_decision.category), "%s", category);
        SDL_snprintf(route_decision.host_action_kind, sizeof(route_decision.host_action_kind), "%s", host_action_kind);
        SDL_snprintf(route_decision.agent_provider, sizeof(route_decision.agent_provider), "%s", agent_provider);
        SDL_snprintf(route_decision.reason, sizeof(route_decision.reason), "%s", reason);
        SDL_snprintf(route_decision.selected_target_label, sizeof(route_decision.selected_target_label), "%s", target_label);
        SDL_snprintf(route_decision.selected_target_id, sizeof(route_decision.selected_target_id), "%s", target_id);
        SDL_snprintf(route_decision.selected_project_id, sizeof(route_decision.selected_project_id), "%s", project_id);
        if (command_text[0] != '\0') {
            route_decision.has_command_text = true;
            SDL_snprintf(route_decision.command_text, sizeof(route_decision.command_text), "%s", command_text);
        }
        if (utterance_id[0] != '\0') {
            sidecar_store_route_response(client, &route_decision);
        }

        if (category[0] != '\0' && target_label[0] != '\0') {
            char detail[TB_SIDECAR_TEXT_MAX];
            SDL_snprintf(detail, sizeof(detail), "sidecar: %s -> %s", category, target_label);
            sidecar_set_detail(client, detail);
        } else if (category[0] != '\0' && target_id[0] != '\0') {
            char detail[TB_SIDECAR_TEXT_MAX];
            SDL_snprintf(detail, sizeof(detail), "sidecar: %s -> %s", category, target_id);
            sidecar_set_detail(client, detail);
        } else if (category[0] != '\0' && project_id[0] != '\0') {
            char detail[TB_SIDECAR_TEXT_MAX];
            SDL_snprintf(detail, sizeof(detail), "sidecar: %s -> %s", category, project_id);
            sidecar_set_detail(client, detail);
        } else if (category[0] != '\0' && reason[0] != '\0') {
            char detail[TB_SIDECAR_TEXT_MAX];
            SDL_snprintf(detail, sizeof(detail), "sidecar: %s | %s", category, reason);
            sidecar_set_detail(client, detail);
        } else if (category[0] != '\0') {
            char detail[TB_SIDECAR_TEXT_MAX];
            SDL_snprintf(detail, sizeof(detail), "sidecar: %s", category);
            sidecar_set_detail(client, detail);
        }
        return;
    }

    if (SDL_strstr(line, "\"kind\":\"project_summary_response\"") != NULL) {
        SDL_zero(project_summary);
        project_id[0] = '\0';
        summary_text[0] = '\0';
        line_extract_string(line, "\"projectId\":\"", project_id, sizeof(project_id));
        line_extract_string(line, "\"summary\":\"", summary_text, sizeof(summary_text));
        SDL_snprintf(project_summary.project_id, sizeof(project_summary.project_id), "%s", project_id);
        SDL_snprintf(project_summary.summary, sizeof(project_summary.summary), "%s", summary_text);
        sidecar_store_project_summary_response(client, &project_summary);
        if (summary_text[0] != '\0') {
            sidecar_set_detail(client, summary_text);
        }
        return;
    }

    if (SDL_strstr(line, "\"kind\":\"target_snapshot_response\"") != NULL) {
        char detail[TB_SIDECAR_TEXT_MAX];

        SDL_zero(target_snapshot);
        target_id[0] = '\0';
        activity_kind[0] = '\0';
        summary_text[0] = '\0';
        recent_text[0] = '\0';
        title[0] = '\0';
        line_extract_string(line, "\"targetId\":\"", target_id, sizeof(target_id));
        line_extract_string(line, "\"activityKind\":\"", activity_kind, sizeof(activity_kind));
        line_extract_string(line, "\"statusSummary\":\"", summary_text, sizeof(summary_text));
        line_extract_string(line, "\"recentText\":\"", recent_text, sizeof(recent_text));
        line_extract_string(line, "\"title\":\"", title, sizeof(title));

        SDL_snprintf(target_snapshot.target_id, sizeof(target_snapshot.target_id), "%s", target_id);
        SDL_snprintf(target_snapshot.activity_kind, sizeof(target_snapshot.activity_kind), "%s", activity_kind);
        SDL_snprintf(target_snapshot.title, sizeof(target_snapshot.title), "%s", title);
        SDL_snprintf(
            target_snapshot.status_summary,
            sizeof(target_snapshot.status_summary),
            "%s",
            summary_text[0] != '\0' ? summary_text : recent_text
        );
        sidecar_store_target_snapshot_response(client, &target_snapshot);

        if (title[0] != '\0' || activity_kind[0] != '\0' || target_snapshot.status_summary[0] != '\0') {
            detail[0] = '\0';
            if (title[0] != '\0' && target_snapshot.status_summary[0] != '\0') {
                SDL_snprintf(
                    detail,
                    sizeof(detail),
                    "%s %s: %s",
                    title,
                    snapshot_activity_phrase(activity_kind),
                    target_snapshot.status_summary
                );
            } else if (title[0] != '\0' && activity_kind[0] != '\0') {
                SDL_snprintf(detail, sizeof(detail), "%s %s.", title, snapshot_activity_phrase(activity_kind));
            } else if (target_snapshot.status_summary[0] != '\0') {
                SDL_snprintf(detail, sizeof(detail), "%s", target_snapshot.status_summary);
            } else if (target_id[0] != '\0') {
                SDL_snprintf(detail, sizeof(detail), "%s %s.", target_id, snapshot_activity_phrase(activity_kind));
            }
            if (detail[0] != '\0') {
                sidecar_set_detail(client, detail);
            }
        }
        return;
    }

    if (SDL_strstr(line, "\"kind\":\"error_response\"") != NULL) {
        message[0] = '\0';
        line_extract_string(line, "\"message\":\"", message, sizeof(message));
        if (message[0] != '\0') {
            sidecar_set_error(client, message);
        } else {
            sidecar_set_error(client, "sidecar returned an error response.");
        }
        sidecar_cancel_pending_requests(client);
    }
}

static int sidecar_reader_thread(void *userdata) {
    TbSidecarClient *client = (TbSidecarClient *) userdata;
    char line[4096];
    DWORD bytes_read = 0;
    size_t length = 0;
    char ch = '\0';

    if (client == NULL || client->stdout_read == NULL) {
        return 0;
    }

    line[0] = '\0';
    while (!client->stop_requested) {
        if (!ReadFile(tb_handle(client->stdout_read), &ch, 1u, &bytes_read, NULL) || bytes_read == 0u) {
            break;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            line[length] = '\0';
            if (length > 0u) {
                sidecar_process_line(client, line);
            }
            length = 0u;
            line[0] = '\0';
            continue;
        }

        if (length + 1u < sizeof(line)) {
            line[length++] = ch;
        }
    }

    sidecar_set_ready(client, false);
    if (!client->stop_requested) {
        sidecar_set_error(client, "sidecar disconnected.");
        sidecar_cancel_pending_requests(client);
    }
    return 0;
}
#endif

void tb_sidecar_client_init(TbSidecarClient *client) {
    if (client == NULL) {
        return;
    }

    SDL_zero(*client);
}

bool tb_sidecar_client_start(TbSidecarClient *client, const char *source_dir) {
    SDL_IOStream *script_stream = NULL;

    if (client == NULL || source_dir == NULL) {
        return false;
    }

    if (client->mutex == NULL) {
        client->mutex = SDL_CreateMutex();
        if (client->mutex == NULL) {
            return false;
        }
    }
    if (client->response_condition == NULL) {
        client->response_condition = SDL_CreateCondition();
        if (client->response_condition == NULL) {
            SDL_DestroyMutex(client->mutex);
            client->mutex = NULL;
            return false;
        }
    }

    if (env_truthy(SDL_getenv("TB_SIDECAR_DISABLED"))) {
        client->disabled = true;
        return false;
    }

    SDL_snprintf(client->script_path, sizeof(client->script_path), "%s/sidecar/dist/index.js", source_dir);
    script_stream = SDL_IOFromFile(client->script_path, "rb");
    if (script_stream == NULL) {
        sidecar_set_error(client, "sidecar is not built. Run `tsc -p sidecar/tsconfig.json` first.");
        return false;
    }
    SDL_CloseIO(script_stream);

#ifdef _WIN32
    {
        SECURITY_ATTRIBUTES sa;
        HANDLE stdout_read = NULL;
        HANDLE stdout_write = NULL;
        HANDLE stdin_read = NULL;
        HANDLE stdin_write = NULL;
        HANDLE stderr_handle = INVALID_HANDLE_VALUE;
        STARTUPINFOA startup_info;
        PROCESS_INFORMATION process_info;
        char command_line[TB_SIDECAR_PATH_MAX * 2u];

        SDL_zero(sa);
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
            sidecar_set_error(client, "CreatePipe failed for sidecar stdout.");
            return false;
        }
        if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            sidecar_set_error(client, "SetHandleInformation failed for sidecar stdout.");
            return false;
        }
        if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            sidecar_set_error(client, "CreatePipe failed for sidecar stdin.");
            return false;
        }
        if (!SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stdin_read);
            CloseHandle(stdin_write);
            sidecar_set_error(client, "SetHandleInformation failed for sidecar stdin.");
            return false;
        }

        stderr_handle = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

        SDL_zero(startup_info);
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESTDHANDLES;
        startup_info.hStdInput = stdin_read;
        startup_info.hStdOutput = stdout_write;
        startup_info.hStdError = stderr_handle != INVALID_HANDLE_VALUE ? stderr_handle : GetStdHandle(STD_ERROR_HANDLE);

        SDL_zero(process_info);
        SDL_snprintf(command_line, sizeof(command_line), "\"node\" \"%s\" --stdio", client->script_path);
        if (!CreateProcessA(
                NULL,
                command_line,
                NULL,
                NULL,
                TRUE,
                CREATE_NO_WINDOW,
                NULL,
                source_dir,
                &startup_info,
                &process_info
            )) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stdin_read);
            CloseHandle(stdin_write);
            if (stderr_handle != INVALID_HANDLE_VALUE) {
                CloseHandle(stderr_handle);
            }
            sidecar_set_error(client, "Could not launch `node sidecar/dist/index.js --stdio`.");
            return false;
        }

        CloseHandle(process_info.hThread);
        CloseHandle(stdout_write);
        CloseHandle(stdin_read);
        if (stderr_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(stderr_handle);
        }

        client->process_handle = process_info.hProcess;
        client->stdin_write = stdin_write;
        client->stdout_read = stdout_read;
        client->running = true;
        client->reader_thread = SDL_CreateThread(sidecar_reader_thread, "sidecar-reader", client);
        if (client->reader_thread == NULL) {
            sidecar_set_error(client, "Could not start the sidecar reader thread.");
            tb_sidecar_client_shutdown(client);
            return false;
        }

        if (!sidecar_write_line(
                client,
                "{\"kind\":\"handshake_request\",\"protocolVersion\":1,\"appId\":\"terminal-buddy-native\",\"capabilities\":{\"localStt\":true,\"localTts\":false,\"widget\":true,\"hotkey\":true}}"
            )) {
            sidecar_set_error(client, "Could not send the initial sidecar handshake.");
            tb_sidecar_client_shutdown(client);
            return false;
        }

        sidecar_set_detail(client, "sidecar: starting");
        return true;
    }
#else
    sidecar_set_error(client, "Sidecar transport is not implemented on this platform.");
    return false;
#endif
}

void tb_sidecar_client_shutdown(TbSidecarClient *client) {
    if (client == NULL) {
        return;
    }

    client->stop_requested = true;

#ifdef _WIN32
    if (client->stdin_write != NULL) {
        CloseHandle(tb_handle(client->stdin_write));
        client->stdin_write = NULL;
    }
    if (client->stdout_read != NULL) {
        CloseHandle(tb_handle(client->stdout_read));
        client->stdout_read = NULL;
    }
#endif

    if (client->reader_thread != NULL) {
        SDL_WaitThread(client->reader_thread, NULL);
        client->reader_thread = NULL;
    }

#ifdef _WIN32
    if (client->process_handle != NULL) {
        WaitForSingleObject(tb_handle(client->process_handle), 1000u);
        CloseHandle(tb_handle(client->process_handle));
        client->process_handle = NULL;
    }
#endif

    if (client->mutex != NULL) {
        SDL_DestroyMutex(client->mutex);
        client->mutex = NULL;
    }
    if (client->response_condition != NULL) {
        SDL_DestroyCondition(client->response_condition);
        client->response_condition = NULL;
    }

    client->running = false;
    client->ready = false;
}

bool tb_sidecar_client_submit_utterance(
    TbSidecarClient *client,
    const char *text,
    const char *active_project_id,
    const char *active_target_id
) {
#ifdef _WIN32
    char timestamp[64];
    char utterance_id[64];
    char *escaped_text = NULL;
    char *escaped_project_id = NULL;
    char *escaped_target_id = NULL;
    char *json = NULL;
    bool ok = false;

    if (client == NULL || text == NULL || text[0] == '\0' || client->disabled || client->stdin_write == NULL) {
        return false;
    }

    format_timestamp_iso8601(timestamp, sizeof(timestamp));
    SDL_snprintf(utterance_id, sizeof(utterance_id), "native-%u", ++client->next_utterance_id);

    escaped_text = json_escape(text);
    escaped_project_id = active_project_id != NULL ? json_escape(active_project_id) : NULL;
    escaped_target_id = active_target_id != NULL ? json_escape(active_target_id) : NULL;
    if (escaped_text == NULL || (active_project_id != NULL && escaped_project_id == NULL) || (active_target_id != NULL && escaped_target_id == NULL)) {
        goto cleanup;
    }

    if (active_project_id != NULL && active_target_id != NULL) {
        SDL_asprintf(
            &json,
            "{\"kind\":\"voice_utterance_request\",\"utteranceId\":\"%s\",\"text\":\"%s\",\"occurredAt\":\"%s\",\"activeProjectId\":\"%s\",\"activeTargetId\":\"%s\"}",
            utterance_id,
            escaped_text,
            timestamp,
            escaped_project_id,
            escaped_target_id
        );
    } else if (active_project_id != NULL) {
        SDL_asprintf(
            &json,
            "{\"kind\":\"voice_utterance_request\",\"utteranceId\":\"%s\",\"text\":\"%s\",\"occurredAt\":\"%s\",\"activeProjectId\":\"%s\"}",
            utterance_id,
            escaped_text,
            timestamp,
            escaped_project_id
        );
    } else {
        SDL_asprintf(
            &json,
            "{\"kind\":\"voice_utterance_request\",\"utteranceId\":\"%s\",\"text\":\"%s\",\"occurredAt\":\"%s\"}",
            utterance_id,
            escaped_text,
            timestamp
        );
    }

    if (json == NULL) {
        goto cleanup;
    }

    ok = sidecar_write_line(client, json);
    if (ok) {
        sidecar_set_detail(client, "sidecar: routing utterance...");
    } else {
        sidecar_set_error(client, "Could not write to the sidecar process.");
    }

cleanup:
    SDL_free(escaped_text);
    SDL_free(escaped_project_id);
    SDL_free(escaped_target_id);
    SDL_free(json);
    return ok;
#else
    (void) client;
    (void) text;
    (void) active_project_id;
    (void) active_target_id;
    return false;
#endif
}

bool tb_sidecar_client_route_utterance(
    TbSidecarClient *client,
    const char *text,
    const char *active_project_id,
    const char *active_target_id,
    Sint32 timeout_ms,
    TbSidecarRouteDecision *decision_out
) {
#ifdef _WIN32
    Uint64 deadline_ms = 0;
    char timestamp[64];
    char utterance_id[64];
    char *escaped_text = NULL;
    char *escaped_project_id = NULL;
    char *escaped_target_id = NULL;
    char *json = NULL;
    bool ok = false;

    if (decision_out != NULL) {
        SDL_zero(*decision_out);
    }

    if (
        client == NULL
        || text == NULL
        || text[0] == '\0'
        || client->disabled
        || client->stdin_write == NULL
        || client->mutex == NULL
        || client->response_condition == NULL
    ) {
        return false;
    }

    format_timestamp_iso8601(timestamp, sizeof(timestamp));
    SDL_snprintf(utterance_id, sizeof(utterance_id), "native-%u", ++client->next_utterance_id);

    escaped_text = json_escape(text);
    escaped_project_id = active_project_id != NULL ? json_escape(active_project_id) : NULL;
    escaped_target_id = active_target_id != NULL ? json_escape(active_target_id) : NULL;
    if (escaped_text == NULL || (active_project_id != NULL && escaped_project_id == NULL) || (active_target_id != NULL && escaped_target_id == NULL)) {
        goto cleanup;
    }

    if (active_project_id != NULL && active_target_id != NULL) {
        SDL_asprintf(
            &json,
            "{\"kind\":\"voice_utterance_request\",\"utteranceId\":\"%s\",\"text\":\"%s\",\"occurredAt\":\"%s\",\"activeProjectId\":\"%s\",\"activeTargetId\":\"%s\"}",
            utterance_id,
            escaped_text,
            timestamp,
            escaped_project_id,
            escaped_target_id
        );
    } else if (active_project_id != NULL) {
        SDL_asprintf(
            &json,
            "{\"kind\":\"voice_utterance_request\",\"utteranceId\":\"%s\",\"text\":\"%s\",\"occurredAt\":\"%s\",\"activeProjectId\":\"%s\"}",
            utterance_id,
            escaped_text,
            timestamp,
            escaped_project_id
        );
    } else {
        SDL_asprintf(
            &json,
            "{\"kind\":\"voice_utterance_request\",\"utteranceId\":\"%s\",\"text\":\"%s\",\"occurredAt\":\"%s\"}",
            utterance_id,
            escaped_text,
            timestamp
        );
    }

    if (json == NULL) {
        goto cleanup;
    }

    SDL_LockMutex(client->mutex);
    sidecar_reset_route_response(client);
    client->awaiting_route_response = true;
    SDL_snprintf(client->pending_utterance_id, sizeof(client->pending_utterance_id), "%s", utterance_id);
    SDL_UnlockMutex(client->mutex);

    ok = sidecar_write_line(client, json);
    if (!ok) {
        SDL_LockMutex(client->mutex);
        sidecar_reset_route_response(client);
        sidecar_wake_route_waiters(client);
        SDL_UnlockMutex(client->mutex);
        sidecar_set_error(client, "Could not write to the sidecar process.");
        goto cleanup;
    }

    sidecar_set_detail(client, "sidecar: routing utterance...");

    deadline_ms = SDL_GetTicks() + (Uint64) (timeout_ms > 0 ? timeout_ms : 0);
    SDL_LockMutex(client->mutex);
    while (client->awaiting_route_response && !client->route_response_ready) {
        Sint32 remaining_ms = 0;
        Uint64 now_ms = SDL_GetTicks();

        if (timeout_ms <= 0) {
            break;
        }

        if (now_ms >= deadline_ms) {
            break;
        }

        remaining_ms = (Sint32) (deadline_ms - now_ms);
        if (!SDL_WaitConditionTimeout(client->response_condition, client->mutex, remaining_ms)) {
            break;
        }
    }

    if (client->route_response_ready) {
        if (decision_out != NULL) {
            *decision_out = client->route_response;
        }
        ok = true;
    } else {
        ok = false;
    }
    sidecar_reset_route_response(client);
    SDL_UnlockMutex(client->mutex);

cleanup:
    SDL_free(escaped_text);
    SDL_free(escaped_project_id);
    SDL_free(escaped_target_id);
    SDL_free(json);
    return ok;
#else
    (void) client;
    (void) text;
    (void) active_project_id;
    (void) active_target_id;
    (void) timeout_ms;
    if (decision_out != NULL) {
        SDL_zero(*decision_out);
    }
    return false;
#endif
}

bool tb_sidecar_client_request_project_summary(
    TbSidecarClient *client,
    const char *project_id,
    bool attention_only,
    Sint32 timeout_ms,
    TbSidecarProjectSummary *summary_out
) {
#ifdef _WIN32
    Uint64 deadline_ms = 0;
    char *escaped_project_id = NULL;
    char *json = NULL;
    bool ok = false;

    if (summary_out != NULL) {
        SDL_zero(*summary_out);
    }

    if (
        client == NULL
        || client->disabled
        || client->stdin_write == NULL
        || client->mutex == NULL
        || client->response_condition == NULL
    ) {
        return false;
    }

    escaped_project_id = project_id != NULL ? json_escape(project_id) : NULL;
    if (project_id != NULL && escaped_project_id == NULL) {
        goto cleanup;
    }

    if (project_id != NULL && project_id[0] != '\0') {
        SDL_asprintf(
            &json,
            "{\"kind\":\"project_summary_request\",\"projectId\":\"%s\",\"scope\":\"%s\"}",
            escaped_project_id,
            attention_only ? "attention_only" : "all_targets"
        );
    } else {
        SDL_asprintf(
            &json,
            "{\"kind\":\"project_summary_request\",\"scope\":\"%s\"}",
            attention_only ? "attention_only" : "all_targets"
        );
    }

    if (json == NULL) {
        goto cleanup;
    }

    SDL_LockMutex(client->mutex);
    sidecar_reset_project_summary_response(client);
    client->awaiting_project_summary_response = true;
    SDL_snprintf(
        client->pending_project_id,
        sizeof(client->pending_project_id),
        "%s",
        (project_id != NULL && project_id[0] != '\0') ? project_id : ""
    );
    SDL_UnlockMutex(client->mutex);

    ok = sidecar_write_line(client, json);
    if (!ok) {
        sidecar_cancel_pending_requests(client);
        sidecar_set_error(client, "Could not request a project summary from the sidecar.");
        goto cleanup;
    }

    deadline_ms = SDL_GetTicks() + (Uint64) (timeout_ms > 0 ? timeout_ms : 0);
    SDL_LockMutex(client->mutex);
    while (client->awaiting_project_summary_response && !client->project_summary_response_ready) {
        Sint32 remaining_ms = 0;
        Uint64 now_ms = SDL_GetTicks();

        if (timeout_ms <= 0) {
            break;
        }
        if (now_ms >= deadline_ms) {
            break;
        }

        remaining_ms = (Sint32) (deadline_ms - now_ms);
        if (!SDL_WaitConditionTimeout(client->response_condition, client->mutex, remaining_ms)) {
            break;
        }
    }

    if (client->project_summary_response_ready) {
        if (summary_out != NULL) {
            *summary_out = client->project_summary_response;
        }
        ok = true;
    } else {
        ok = false;
    }
    sidecar_reset_project_summary_response(client);
    SDL_UnlockMutex(client->mutex);

cleanup:
    SDL_free(escaped_project_id);
    SDL_free(json);
    return ok;
#else
    (void) client;
    (void) project_id;
    (void) attention_only;
    (void) timeout_ms;
    if (summary_out != NULL) {
        SDL_zero(*summary_out);
    }
    return false;
#endif
}

bool tb_sidecar_client_request_target_snapshot(
    TbSidecarClient *client,
    const char *target_id,
    Sint32 timeout_ms,
    TbSidecarTargetSnapshot *snapshot_out
) {
#ifdef _WIN32
    Uint64 deadline_ms = 0;
    char *escaped_target_id = NULL;
    char *json = NULL;
    bool ok = false;

    if (snapshot_out != NULL) {
        SDL_zero(*snapshot_out);
    }

    if (
        client == NULL
        || target_id == NULL
        || target_id[0] == '\0'
        || client->disabled
        || client->stdin_write == NULL
        || client->mutex == NULL
        || client->response_condition == NULL
    ) {
        return false;
    }

    escaped_target_id = json_escape(target_id);
    if (escaped_target_id == NULL) {
        goto cleanup;
    }

    SDL_asprintf(
        &json,
        "{\"kind\":\"target_snapshot_request\",\"targetId\":\"%s\"}",
        escaped_target_id
    );
    if (json == NULL) {
        goto cleanup;
    }

    SDL_LockMutex(client->mutex);
    sidecar_reset_target_snapshot_response(client);
    client->awaiting_target_snapshot_response = true;
    SDL_snprintf(client->pending_target_id, sizeof(client->pending_target_id), "%s", target_id);
    SDL_UnlockMutex(client->mutex);

    ok = sidecar_write_line(client, json);
    if (!ok) {
        sidecar_cancel_pending_requests(client);
        sidecar_set_error(client, "Could not request a target snapshot from the sidecar.");
        goto cleanup;
    }

    deadline_ms = SDL_GetTicks() + (Uint64) (timeout_ms > 0 ? timeout_ms : 0);
    SDL_LockMutex(client->mutex);
    while (client->awaiting_target_snapshot_response && !client->target_snapshot_response_ready) {
        Sint32 remaining_ms = 0;
        Uint64 now_ms = SDL_GetTicks();

        if (timeout_ms <= 0) {
            break;
        }
        if (now_ms >= deadline_ms) {
            break;
        }

        remaining_ms = (Sint32) (deadline_ms - now_ms);
        if (!SDL_WaitConditionTimeout(client->response_condition, client->mutex, remaining_ms)) {
            break;
        }
    }

    if (client->target_snapshot_response_ready) {
        if (snapshot_out != NULL) {
            *snapshot_out = client->target_snapshot_response;
        }
        ok = true;
    } else {
        ok = false;
    }
    sidecar_reset_target_snapshot_response(client);
    SDL_UnlockMutex(client->mutex);

cleanup:
    SDL_free(escaped_target_id);
    SDL_free(json);
    return ok;
#else
    (void) client;
    (void) target_id;
    (void) timeout_ms;
    if (snapshot_out != NULL) {
        SDL_zero(*snapshot_out);
    }
    return false;
#endif
}

bool tb_sidecar_client_upsert_observed_target(
    TbSidecarClient *client,
    const char *target_kind,
    const char *provider,
    const char *target_id,
    const char *project_id,
    const char *cwd,
    const char *label,
    const char *window_id,
    Uint32 process_id,
    const char *process_name
) {
#ifdef _WIN32
    return tb_sidecar_client_upsert_window_target(
        client,
        target_kind,
        provider,
        target_id,
        project_id,
        cwd,
        label,
        "idle",
        "low",
        window_id,
        process_id,
        process_name,
        NULL,
        NULL,
        NULL
    );
#else
    (void) client;
    (void) target_kind;
    (void) provider;
    (void) target_id;
    (void) project_id;
    (void) cwd;
    (void) label;
    (void) window_id;
    (void) process_id;
    (void) process_name;
    return false;
#endif
}

bool tb_sidecar_client_register_observed_agent_launch(
    TbSidecarClient *client,
    const char *agent_provider,
    const char *shell_target_id,
    const char *project_id,
    const char *cwd,
    const char *shell_label,
    const char *window_id,
    Uint32 process_id,
    const char *process_name
) {
#ifdef _WIN32
    char agent_target_id[TB_SIDECAR_ID_MAX];
    const char *agent_label = NULL;
    bool agent_ok = false;
    bool shell_ok = false;

    if (
        client == NULL
        || agent_provider == NULL
        || agent_provider[0] == '\0'
        || shell_target_id == NULL
        || shell_target_id[0] == '\0'
        || project_id == NULL
        || project_id[0] == '\0'
        || window_id == NULL
        || window_id[0] == '\0'
    ) {
        return false;
    }

    if (SDL_strcmp(agent_provider, "claude") != 0 && SDL_strcmp(agent_provider, "codex") != 0) {
        return false;
    }

    SDL_snprintf(agent_target_id, sizeof(agent_target_id), "%s:%s", agent_provider, window_id);
    agent_label = SDL_strcmp(agent_provider, "claude") == 0 ? "Claude session" : "Codex session";

    agent_ok = tb_sidecar_client_upsert_window_target(
        client,
        "observed_agent",
        agent_provider,
        agent_target_id,
        project_id,
        cwd,
        agent_label,
        "launching_agent",
        "low",
        window_id,
        process_id,
        process_name,
        NULL,
        NULL,
        shell_target_id
    );
    shell_ok = tb_sidecar_client_upsert_window_target(
        client,
        "observed_shell",
        "shell",
        shell_target_id,
        project_id,
        cwd,
        shell_label,
        "launching_agent",
        "low",
        window_id,
        process_id,
        process_name,
        "agent_host",
        agent_target_id,
        NULL
    );

    return agent_ok && shell_ok;
#else
    (void) client;
    (void) agent_provider;
    (void) shell_target_id;
    (void) project_id;
    (void) cwd;
    (void) shell_label;
    (void) window_id;
    (void) process_id;
    (void) process_name;
    return false;
#endif
}

#ifdef _WIN32
bool tb_sidecar_client_upsert_window_target(
    TbSidecarClient *client,
    const char *target_kind,
    const char *provider,
    const char *target_id,
    const char *project_id,
    const char *cwd,
    const char *label,
    const char *status,
    const char *attention,
    const char *window_id,
    Uint32 process_id,
    const char *process_name,
    const char *occupancy,
    const char *attached_agent_target_id,
    const char *host_shell_target_id
) {
    char *escaped_target_id = NULL;
    char *escaped_project_id = NULL;
    char *escaped_cwd = NULL;
    char *escaped_label = NULL;
    char *escaped_window_id = NULL;
    char *escaped_process_name = NULL;
    char *escaped_occupancy = NULL;
    char *escaped_attached_agent_target_id = NULL;
    char *escaped_host_shell_target_id = NULL;
    char *occupancy_field = NULL;
    char *attached_agent_field = NULL;
    char *host_shell_field = NULL;
    char *json = NULL;
    bool ok = false;

    if (client == NULL || client->disabled || client->stdin_write == NULL) {
        return false;
    }
    if (
        target_kind == NULL || target_kind[0] == '\0'
        || provider == NULL || provider[0] == '\0'
        || target_id == NULL || target_id[0] == '\0'
        || project_id == NULL || project_id[0] == '\0'
    ) {
        return false;
    }

    escaped_target_id = json_escape(target_id);
    escaped_project_id = json_escape(project_id);
    escaped_cwd = json_escape(cwd != NULL ? cwd : "");
    escaped_label = json_escape(label != NULL ? label : target_id);
    escaped_window_id = json_escape(window_id != NULL ? window_id : target_id);
    escaped_process_name = json_escape(process_name != NULL ? process_name : "");
    escaped_occupancy = occupancy != NULL && occupancy[0] != '\0' ? json_escape(occupancy) : NULL;
    escaped_attached_agent_target_id = attached_agent_target_id != NULL && attached_agent_target_id[0] != '\0'
        ? json_escape(attached_agent_target_id)
        : NULL;
    escaped_host_shell_target_id = host_shell_target_id != NULL && host_shell_target_id[0] != '\0'
        ? json_escape(host_shell_target_id)
        : NULL;
    if (
        escaped_target_id == NULL
        || escaped_project_id == NULL
        || escaped_cwd == NULL
        || escaped_label == NULL
        || escaped_window_id == NULL
        || escaped_process_name == NULL
        || (occupancy != NULL && occupancy[0] != '\0' && escaped_occupancy == NULL)
        || (attached_agent_target_id != NULL && attached_agent_target_id[0] != '\0' && escaped_attached_agent_target_id == NULL)
        || (host_shell_target_id != NULL && host_shell_target_id[0] != '\0' && escaped_host_shell_target_id == NULL)
    ) {
        goto cleanup;
    }

    occupancy_field = SDL_strdup("");
    attached_agent_field = SDL_strdup("");
    host_shell_field = SDL_strdup("");
    if (occupancy_field == NULL || attached_agent_field == NULL || host_shell_field == NULL) {
        goto cleanup;
    }

    if (escaped_occupancy != NULL) {
        SDL_free(occupancy_field);
        occupancy_field = NULL;
        if (SDL_asprintf(&occupancy_field, "\"occupancy\":\"%s\",", escaped_occupancy) < 0) {
            occupancy_field = NULL;
            goto cleanup;
        }
    }
    if (escaped_attached_agent_target_id != NULL) {
        SDL_free(attached_agent_field);
        attached_agent_field = NULL;
        if (SDL_asprintf(
                &attached_agent_field,
                "\"attachedAgentTargetId\":\"%s\",",
                escaped_attached_agent_target_id
            ) < 0) {
            attached_agent_field = NULL;
            goto cleanup;
        }
    }
    if (escaped_host_shell_target_id != NULL) {
        SDL_free(host_shell_field);
        host_shell_field = NULL;
        if (SDL_asprintf(
                &host_shell_field,
                "\"hostShellTargetId\":\"%s\",",
                escaped_host_shell_target_id
            ) < 0) {
            host_shell_field = NULL;
            goto cleanup;
        }
    }

    if (SDL_asprintf(
            &json,
            "{\"kind\":\"target_upsert_request\",\"target\":{\"id\":\"%s\",\"kind\":\"%s\",\"projectId\":\"%s\",\"provider\":\"%s\",\"cwd\":\"%s\",\"label\":\"%s\",\"status\":\"%s\",\"attention\":\"%s\",%s%s%s\"transport\":{\"transportKind\":\"window\",\"platform\":\"windows\",\"windowId\":\"%s\",\"processId\":%u,\"processName\":\"%s\"}}}",
            escaped_target_id,
            target_kind,
            escaped_project_id,
            provider,
            escaped_cwd,
            escaped_label,
            (status != NULL && status[0] != '\0') ? status : "idle",
            (attention != NULL && attention[0] != '\0') ? attention : "low",
            occupancy_field,
            attached_agent_field,
            host_shell_field,
            escaped_window_id,
            (unsigned int) process_id,
            escaped_process_name
        ) < 0) {
        json = NULL;
        goto cleanup;
    }

    ok = sidecar_write_line(client, json);
    if (!ok) {
        sidecar_set_error(client, "Could not register the active shell target with the sidecar.");
    }

cleanup:
    SDL_free(escaped_target_id);
    SDL_free(escaped_project_id);
    SDL_free(escaped_cwd);
    SDL_free(escaped_label);
    SDL_free(escaped_window_id);
    SDL_free(escaped_process_name);
    SDL_free(escaped_occupancy);
    SDL_free(escaped_attached_agent_target_id);
    SDL_free(escaped_host_shell_target_id);
    SDL_free(occupancy_field);
    SDL_free(attached_agent_field);
    SDL_free(host_shell_field);
    SDL_free(json);
    return ok;
}
#endif

void tb_sidecar_client_copy_snapshot(
    TbSidecarClient *client,
    bool *ready_out,
    char *detail_out,
    size_t detail_out_size
) {
    const char *detail_source = "";

    if (ready_out != NULL) {
        *ready_out = false;
    }
    if (detail_out != NULL && detail_out_size > 0) {
        detail_out[0] = '\0';
    }
    if (client == NULL || client->mutex == NULL) {
        return;
    }

    SDL_LockMutex(client->mutex);
    if (ready_out != NULL) {
        *ready_out = client->ready;
    }
    if (client->error_text[0] != '\0') {
        detail_source = client->error_text;
    } else if (client->detail_text[0] != '\0') {
        detail_source = client->detail_text;
    }
    if (detail_out != NULL && detail_out_size > 0) {
        SDL_snprintf(detail_out, detail_out_size, "%s", detail_source);
    }
    SDL_UnlockMutex(client->mutex);
}
