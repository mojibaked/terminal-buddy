#include "ui_clay_internal.h"

void tb_ui_build_idle_shell(const TbUiModel *model) {
    CLAY(CLAY_ID("IdleShell"), {
        .floating = {
            .attachTo = CLAY_ATTACH_TO_PARENT,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_CENTER_CENTER,
                .parent = CLAY_ATTACH_POINT_CENTER_CENTER
            }
        },
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(tb_ui_idle_shell_width(model)), CLAY_SIZING_FIXED(tb_ui_idle_shell_height(model)) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        CLAY(CLAY_ID("IdleShellButtons"), {
            .layout = {
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .childGap = tb_ui_scale_u16(model, 10),
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            }
        }) {
            tb_ui_build_terminal_button(model);
            tb_ui_build_button(model, false);
        }
    }
}

void tb_ui_build_layout(const TbUiModel *model) {
    CLAY(CLAY_ID("Root"), {
        .layout = {
            .sizing = {
                CLAY_SIZING_FIXED((float) model->window_width),
                CLAY_SIZING_FIXED((float) model->window_height)
            }
        }
    }) {
        if (model->scene == TB_UI_SCENE_IDLE) {
            tb_ui_build_idle_shell(model);
        } else {
            tb_ui_build_expanded_shell(model);
        }
    }
}
