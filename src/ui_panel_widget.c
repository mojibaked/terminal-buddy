#include "ui_clay_internal.h"

static void tb_ui_build_listening_bars(const TbUiModel *model) {
    float bar_height = tb_ui_scale_value(model, 54);
    uint16_t child_gap = tb_ui_scale_u16(model, 4);

    CLAY(CLAY_ID("ListeningBars"), {
        .layout = {
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(bar_height) },
            .childGap = child_gap,
            .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM }
        }
    }) {
        for (int index = 0; index < model->audio_history_count; ++index) {
            float sample = model->audio_history != NULL ? model->audio_history[index] : 0.0f;
            float height = tb_ui_scale_value(model, 10.0f + SDL_roundf(sample * 44.0f));
            Uint8 alpha = index >= model->audio_history_count - 4 ? 220 : 132;

            CLAY(CLAY_IDI("ListeningBar", index), {
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(tb_ui_scale_value(model, 6)), CLAY_SIZING_FIXED(height) }
                },
                .backgroundColor = tb_ui_color(255, 96, 96, alpha),
                .cornerRadius = CLAY_CORNER_RADIUS(tb_ui_scale_value(model, 3))
            }) {}
        }
    }
}

static void tb_ui_build_processing_bars(const TbUiModel *model) {
    float bar_height = tb_ui_scale_value(model, 54);
    uint16_t child_gap = tb_ui_scale_u16(model, 8);

    CLAY(CLAY_ID("ProcessingBars"), {
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(bar_height) },
            .childGap = child_gap
        }
    }) {
        for (int index = 0; index < 5; ++index) {
            float phase = (float) (((model->ticks_ms / 110u) + (uint64_t) index) % 6u);
            float width = tb_ui_scale_value(model, 28.0f + SDL_roundf(phase * 12.0f));

            CLAY(CLAY_IDI("ProcessingBar", index), {
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(width), CLAY_SIZING_FIXED(tb_ui_scale_value(model, 6)) }
                },
                .backgroundColor = tb_ui_color(255, 96, 96, index == 0 ? 180 : 120),
                .cornerRadius = CLAY_CORNER_RADIUS(tb_ui_scale_value(model, 3))
            }) {}
        }
    }
}

void tb_ui_build_expanded_shell(const TbUiModel *model) {
    bool active = model->scene == TB_UI_SCENE_LISTENING || model->scene == TB_UI_SCENE_PROCESSING;
    Clay_TextElementConfig title_text = CLAY_TEXT_CONFIG({
        .fontId = TB_UI_FONT_ID_BODY,
        .fontSize = tb_ui_scale_u16(model, 13),
        .textColor = {255, 255, 255, 214}
    });
    Clay_TextElementConfig subtitle_text = CLAY_TEXT_CONFIG({
        .fontId = TB_UI_FONT_ID_BODY,
        .fontSize = tb_ui_scale_u16(model, 11),
        .textColor = {255, 255, 255, 136}
    });
    Clay_TextElementConfig transcript_text = CLAY_TEXT_CONFIG({
        .fontId = TB_UI_FONT_ID_BODY,
        .fontSize = tb_ui_scale_u16(model, 12),
        .lineHeight = tb_ui_scale_u16(model, 15),
        .textColor = {255, 255, 255, 164}
    });
    const char *title = model->status_text != NULL ? model->status_text : "TRANSCRIPT";

    if (model->scene == TB_UI_SCENE_LISTENING) {
        title = "LISTENING";
    } else if (model->scene == TB_UI_SCENE_PROCESSING) {
        title = "TRANSCRIBING";
    }

    CLAY(CLAY_ID("Shell"), {
        .floating = {
            .attachTo = CLAY_ATTACH_TO_PARENT,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_CENTER_CENTER,
                .parent = CLAY_ATTACH_POINT_CENTER_CENTER
            }
        },
        .layout = {
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .sizing = { CLAY_SIZING_FIXED(tb_ui_shell_width(model)), CLAY_SIZING_FIXED(tb_ui_shell_height(model)) },
            .padding = CLAY_PADDING_ALL(tb_ui_scale_u16(model, 14)),
            .childGap = tb_ui_scale_u16(model, 16),
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = tb_ui_color(14, 17, 24, 214),
        .cornerRadius = CLAY_CORNER_RADIUS(tb_ui_shell_radius(model)),
        .border = {
            .color = tb_ui_color(255, 255, 255, 22),
            .width = {1, 1, 1, 1}
        }
    }) {
        CLAY(CLAY_ID("ShellButtonRail"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(tb_ui_scale_value(model, 92)), CLAY_SIZING_GROW(0) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            }
        }) {
            tb_ui_build_button(model, active);
        }

        CLAY(CLAY_ID("ShellContent"), {
            .layout = {
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .childGap = tb_ui_scale_u16(model, 8)
            }
        }) {
            CLAY(CLAY_ID("ShellTitleRow"), {
                .layout = {
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(tb_ui_scale_value(model, 16)) },
                    .childGap = tb_ui_scale_u16(model, 8)
                }
            }) {
                CLAY_TEXT(tb_ui_string(title), title_text);
                if (model->scene == TB_UI_SCENE_LISTENING && model->mode_label != NULL) {
                    CLAY_TEXT(tb_ui_string(model->mode_label), subtitle_text);
                }
            }

            if (model->scene == TB_UI_SCENE_LISTENING) {
                tb_ui_build_listening_bars(model);
                CLAY_TEXT(tb_ui_string("tap again to stop"), subtitle_text);
            } else if (model->scene == TB_UI_SCENE_PROCESSING) {
                tb_ui_build_processing_bars(model);
                CLAY_TEXT(tb_ui_string("sending clip to OpenAI"), subtitle_text);
            } else {
                CLAY(CLAY_ID("TranscriptBlock"), {
                    .layout = {
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }
                    }
                }) {
                    if (model->transcript_text != NULL && model->transcript_text[0] != '\0') {
                        CLAY_TEXT(tb_ui_string(model->transcript_text), transcript_text);
                    } else {
                        CLAY_TEXT(tb_ui_string("No transcript text returned."), transcript_text);
                    }
                }
            }
        }
    }
}
