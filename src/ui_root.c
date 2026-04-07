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
            .sizing = { CLAY_SIZING_FIXED(tb_ui_button_size(model)), CLAY_SIZING_FIXED(tb_ui_button_size(model)) }
        }
    }) {
        tb_ui_build_button(model, false);
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
        if (model->scene == TB_UI_SCENE_KEYBOARD) {
            tb_ui_build_keyboard_shell(model);
        } else if (model->scene == TB_UI_SCENE_IDLE) {
            tb_ui_build_idle_shell(model);
        } else {
            tb_ui_build_expanded_shell(model);
        }
    }
}
