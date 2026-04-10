#ifndef TERMINAL_BUDDY_UI_CLAY_H
#define TERMINAL_BUDDY_UI_CLAY_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

typedef enum TbUiTextRenderMode {
    TB_UI_TEXT_RENDER_SURFACE = 0,
    TB_UI_TEXT_RENDER_ENGINE
} TbUiTextRenderMode;

typedef enum TbUiScene {
    TB_UI_SCENE_IDLE = 0,
    TB_UI_SCENE_LISTENING,
    TB_UI_SCENE_PROCESSING,
    TB_UI_SCENE_FEEDBACK
} TbUiScene;

typedef struct TbUiModel {
    int window_width;
    int window_height;
    float ui_scale;
    int mode;
    TbUiScene scene;
    bool terminal_button_active;
    float pulse;
    float audio_level;
    uint64_t ticks_ms;
    const float *audio_history;
    int audio_history_count;
    const char *mode_label;
    const char *backend_label;
    const char *status_text;
    const char *metrics_text;
    const char *transcript_text;
    const char *primary_action_label;
} TbUiModel;

bool tb_ui_init(SDL_Renderer *renderer);
void tb_ui_shutdown(void);
void tb_ui_set_text_render_mode(TbUiTextRenderMode mode);
TbUiTextRenderMode tb_ui_get_text_render_mode(void);
void tb_ui_set_text_debug_logging(bool enabled);
bool tb_ui_get_text_debug_logging(void);
bool tb_ui_render(const TbUiModel *model);
bool tb_ui_primary_action_contains(float x, float y);
bool tb_ui_bubble_contains(float x, float y);
bool tb_ui_terminal_button_contains(float x, float y);

#endif
