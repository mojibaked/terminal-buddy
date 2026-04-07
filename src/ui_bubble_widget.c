#include "ui_clay_internal.h"

void tb_ui_build_button(const TbUiModel *model, bool active) {
    TbUiColors mode_colors = tb_ui_colors_for_mode(model->mode);
    Clay_Color glow = active ? tb_ui_active_glow() : mode_colors.glow;
    Clay_Color face = active ? tb_ui_active_red() : tb_ui_color(20, 26, 40, 220);
    Clay_Color border = active ? tb_ui_color(255, 164, 164, 90) : tb_ui_color(255, 255, 255, 28);
    float button_size = tb_ui_button_size(model);
    float glow_size = tb_ui_button_glow_size(model) + (active ? tb_ui_scale_value(model, SDL_roundf(model->pulse * 8.0f)) : 0.0f);

    CLAY(CLAY_ID("BubbleRoot"), {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(button_size), CLAY_SIZING_FIXED(button_size) }
        }
    }) {
        CLAY(CLAY_ID("BubbleGlow"), {
            .floating = {
                .attachTo = CLAY_ATTACH_TO_PARENT,
                .zIndex = 0,
                .attachPoints = {
                    .element = CLAY_ATTACH_POINT_CENTER_CENTER,
                    .parent = CLAY_ATTACH_POINT_CENTER_CENTER
                }
            },
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(glow_size), CLAY_SIZING_FIXED(glow_size) }
            },
            .backgroundColor = glow,
            .cornerRadius = CLAY_CORNER_RADIUS(glow_size / 2.0f)
        }) {}

        CLAY(CLAY_ID("BubbleFace"), {
            .floating = {
                .attachTo = CLAY_ATTACH_TO_PARENT,
                .zIndex = 1,
                .attachPoints = {
                    .element = CLAY_ATTACH_POINT_CENTER_CENTER,
                    .parent = CLAY_ATTACH_POINT_CENTER_CENTER
                }
            },
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(button_size), CLAY_SIZING_FIXED(button_size) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = face,
            .cornerRadius = CLAY_CORNER_RADIUS(button_size / 2.0f),
            .border = {
                .color = border,
                .width = {1, 1, 1, 1}
            }
        }) {
            if (active) {
                float halo_size = tb_ui_scale_value(model, 46.0f + SDL_roundf(model->audio_level * 8.0f));

                CLAY(CLAY_ID("BubbleInnerHalo"), {
                    .layout = {
                        .sizing = { CLAY_SIZING_FIXED(halo_size), CLAY_SIZING_FIXED(halo_size) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                    },
                    .backgroundColor = tb_ui_color(255, 205, 205, 42),
                    .cornerRadius = CLAY_CORNER_RADIUS(halo_size / 2.0f)
                }) {
                    CLAY(CLAY_ID("BubbleStop"), {
                        .layout = {
                            .sizing = { CLAY_SIZING_FIXED(tb_ui_scale_value(model, 20)), CLAY_SIZING_FIXED(tb_ui_scale_value(model, 20)) }
                        },
                        .backgroundColor = tb_ui_color(255, 255, 255, 255),
                        .cornerRadius = CLAY_CORNER_RADIUS(tb_ui_scale_value(model, 4))
                    }) {}
                }
            } else {
                CLAY(CLAY_ID("BubbleAccent"), {
                    .layout = {
                        .sizing = { CLAY_SIZING_FIXED(tb_ui_scale_value(model, 56)), CLAY_SIZING_FIXED(tb_ui_scale_value(model, 56)) }
                    },
                    .backgroundColor = mode_colors.accent,
                    .cornerRadius = CLAY_CORNER_RADIUS(tb_ui_scale_value(model, 28))
                }) {}
            }
        }
    }
}
