#ifndef TERMINAL_BUDDY_SIDECAR_CLIENT_H
#define TERMINAL_BUDDY_SIDECAR_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL.h>

#define TB_SIDECAR_ID_MAX 128
#define TB_SIDECAR_TEXT_MAX 512
#define TB_SIDECAR_CATEGORY_MAX 64
#define TB_SIDECAR_COMMAND_MAX 2048
#define TB_SIDECAR_PATH_MAX 512
#define TB_SIDECAR_SUMMARY_MAX 2048

typedef struct TbSidecarRouteDecision {
    bool received;
    char utterance_id[64];
    char category[TB_SIDECAR_CATEGORY_MAX];
    char host_action_kind[TB_SIDECAR_CATEGORY_MAX];
    char agent_provider[TB_SIDECAR_CATEGORY_MAX];
    char reason[TB_SIDECAR_TEXT_MAX];
    char selected_project_id[TB_SIDECAR_ID_MAX];
    char selected_target_id[TB_SIDECAR_ID_MAX];
    char selected_target_label[TB_SIDECAR_TEXT_MAX];
    bool has_command_text;
    char command_text[TB_SIDECAR_COMMAND_MAX];
    bool submit;
} TbSidecarRouteDecision;

typedef struct TbSidecarProjectSummary {
    bool received;
    char project_id[TB_SIDECAR_ID_MAX];
    char summary[TB_SIDECAR_SUMMARY_MAX];
} TbSidecarProjectSummary;

typedef struct TbSidecarTargetSnapshot {
    bool received;
    char target_id[TB_SIDECAR_ID_MAX];
    char activity_kind[TB_SIDECAR_CATEGORY_MAX];
    char title[TB_SIDECAR_TEXT_MAX];
    char status_summary[TB_SIDECAR_SUMMARY_MAX];
} TbSidecarTargetSnapshot;

typedef struct TbSidecarClient {
    SDL_Mutex *mutex;
    SDL_Condition *response_condition;
    SDL_Thread *reader_thread;
    bool disabled;
    bool stop_requested;
    bool running;
    bool ready;
    bool awaiting_route_response;
    bool route_response_ready;
    bool awaiting_project_summary_response;
    bool project_summary_response_ready;
    bool awaiting_target_snapshot_response;
    bool target_snapshot_response_ready;
    Uint32 next_utterance_id;
    char script_path[TB_SIDECAR_PATH_MAX];
    char pending_utterance_id[64];
    char pending_project_id[TB_SIDECAR_ID_MAX];
    char pending_target_id[TB_SIDECAR_ID_MAX];
    char detail_text[TB_SIDECAR_TEXT_MAX];
    char error_text[TB_SIDECAR_TEXT_MAX];
    TbSidecarRouteDecision route_response;
    TbSidecarProjectSummary project_summary_response;
    TbSidecarTargetSnapshot target_snapshot_response;
#ifdef _WIN32
    void *process_handle;
    void *stdin_write;
    void *stdout_read;
#endif
} TbSidecarClient;

void tb_sidecar_client_init(TbSidecarClient *client);
bool tb_sidecar_client_start(TbSidecarClient *client, const char *source_dir);
void tb_sidecar_client_shutdown(TbSidecarClient *client);
bool tb_sidecar_client_submit_utterance(
    TbSidecarClient *client,
    const char *text,
    const char *active_project_id,
    const char *active_target_id
);
bool tb_sidecar_client_route_utterance(
    TbSidecarClient *client,
    const char *text,
    const char *active_project_id,
    const char *active_target_id,
    Sint32 timeout_ms,
    TbSidecarRouteDecision *decision_out
);
bool tb_sidecar_client_request_project_summary(
    TbSidecarClient *client,
    const char *project_id,
    bool attention_only,
    Sint32 timeout_ms,
    TbSidecarProjectSummary *summary_out
);
bool tb_sidecar_client_request_target_snapshot(
    TbSidecarClient *client,
    const char *target_id,
    Sint32 timeout_ms,
    TbSidecarTargetSnapshot *snapshot_out
);
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
);
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
);
void tb_sidecar_client_copy_snapshot(
    TbSidecarClient *client,
    bool *ready_out,
    char *detail_out,
    size_t detail_out_size
);

#endif
