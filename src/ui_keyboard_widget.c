#include "ui_clay_internal.h"

#include "keyboard_layout.h"

static Clay_Color tb_ui_keyboard_shell_color(void) {
    return tb_ui_color(14, 17, 24, 222);
}

static Clay_Color tb_ui_keyboard_key_color(bool active, bool pressed) {
    if (pressed) {
        return tb_ui_color(255, 176, 90, 255);
    }
    if (active) {
        return tb_ui_color(70, 118, 214, 255);
    }
    return tb_ui_color(27, 34, 49, 236);
}

static Clay_Color tb_ui_keyboard_key_border(bool active, bool pressed) {
    if (pressed) {
        return tb_ui_color(255, 223, 176, 196);
    }
    if (active) {
        return tb_ui_color(182, 214, 255, 164);
    }
    return tb_ui_color(255, 255, 255, 30);
}

void tb_ui_build_keyboard_shell(const TbUiModel *model) {
    Clay_TextElementConfig title_text = CLAY_TEXT_CONFIG({
        .fontId = TB_UI_FONT_ID_BODY,
        .fontSize = tb_ui_scale_u16(model, 14),
        .textColor = {255, 255, 255, 224}
    });
    Clay_TextElementConfig subtitle_text = CLAY_TEXT_CONFIG({
        .fontId = TB_UI_FONT_ID_BODY,
        .fontSize = tb_ui_scale_u16(model, 11),
        .textColor = {255, 255, 255, 140}
    });
    Clay_TextElementConfig key_text = CLAY_TEXT_CONFIG({
        .fontId = TB_UI_FONT_ID_BODY,
        .fontSize = tb_ui_scale_u16(model, 12),
        .textColor = {255, 255, 255, 232}
    });
    Clay_TextElementConfig modifier_text = CLAY_TEXT_CONFIG({
        .fontId = TB_UI_FONT_ID_BODY,
        .fontSize = tb_ui_scale_u16(model, 11),
        .textColor = {255, 255, 255, 176}
    });
    const TbKeyboardLayoutResult *layout = model->keyboard_layout;

    if (layout == NULL || model->keyboard_mods == NULL || model->keyboard_labels == NULL) {
        return;
    }

    CLAY(CLAY_ID("KeyboardShell"), {
        .floating = {
            .attachTo = CLAY_ATTACH_TO_PARENT,
            .offset = {layout->shell_rect.x, layout->shell_rect.y}
        },
        .layout = {
            .sizing = {
                CLAY_SIZING_FIXED(layout->shell_rect.w),
                CLAY_SIZING_FIXED(layout->shell_rect.h)
            }
        },
        .backgroundColor = tb_ui_keyboard_shell_color(),
        .cornerRadius = CLAY_CORNER_RADIUS(tb_ui_scale_value(model, 34)),
        .border = {
            .color = tb_ui_color(255, 255, 255, 22),
            .width = {1, 1, 1, 1}
        }
    }) {
        CLAY(CLAY_ID("KeyboardBubbleRail"), {
            .floating = {
                .attachTo = CLAY_ATTACH_TO_PARENT,
                .offset = {layout->bubble_rect.x - layout->shell_rect.x, layout->bubble_rect.y - layout->shell_rect.y}
            },
            .layout = {
                .sizing = {
                    CLAY_SIZING_FIXED(layout->bubble_rect.w),
                    CLAY_SIZING_FIXED(layout->bubble_rect.h)
                }
            }
        }) {
            tb_ui_build_button(model, false);
        }

        CLAY(CLAY_ID("KeyboardTitle"), {
            .floating = {
                .attachTo = CLAY_ATTACH_TO_PARENT,
                .offset = {layout->header_rect.x - layout->shell_rect.x, layout->header_rect.y - layout->shell_rect.y}
            },
            .layout = {
                .sizing = {
                    CLAY_SIZING_FIXED(layout->header_rect.w),
                    CLAY_SIZING_FIXED(layout->header_rect.h)
                }
            }
        }) {
            CLAY_TEXT(tb_ui_string("Terminal Keyboard"), title_text);
        }

        CLAY(CLAY_ID("KeyboardSubtitle"), {
            .floating = {
                .attachTo = CLAY_ATTACH_TO_PARENT,
                .offset = {layout->subtitle_rect.x - layout->shell_rect.x, layout->subtitle_rect.y - layout->shell_rect.y}
            },
            .layout = {
                .sizing = {
                    CLAY_SIZING_FIXED(layout->subtitle_rect.w),
                    CLAY_SIZING_FIXED(layout->subtitle_rect.h)
                }
            }
        }) {
            CLAY_TEXT(tb_ui_string("Hold the bubble from idle to open. Tap the bubble to close."), subtitle_text);
        }

        CLAY(CLAY_ID("KeyboardModifiers"), {
            .floating = {
                .attachTo = CLAY_ATTACH_TO_PARENT,
                .offset = {layout->modifier_rect.x - layout->shell_rect.x, layout->modifier_rect.y - layout->shell_rect.y}
            },
            .layout = {
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .childGap = tb_ui_scale_u16(model, 10),
                .sizing = {
                    CLAY_SIZING_FIXED(layout->modifier_rect.w),
                    CLAY_SIZING_FIXED(layout->modifier_rect.h)
                }
            }
        }) {
            CLAY_TEXT(tb_ui_string(model->keyboard_mods->ctrl ? "CTRL" : "ctrl"), modifier_text);
            CLAY_TEXT(tb_ui_string(model->keyboard_mods->alt ? "ALT" : "alt"), modifier_text);
            CLAY_TEXT(tb_ui_string(model->keyboard_mods->caps_lock ? "CAPS" : (model->keyboard_mods->shift ? "SHIFT" : "shift")), modifier_text);
        }

        for (int index = 0; index < layout->key_count; ++index) {
            const TbKeyboardKeyRect *key = &layout->keys[index];
            bool active = tb_keyboard_spec_is_active(key->spec, model->keyboard_mods);
            bool pressed = model->keyboard_pressed_key == index;

            CLAY(CLAY_IDI("KeyboardKey", index), {
                .floating = {
                    .attachTo = CLAY_ATTACH_TO_PARENT,
                    .offset = {key->rect.x - layout->shell_rect.x, key->rect.y - layout->shell_rect.y}
                },
                .layout = {
                    .sizing = {
                        CLAY_SIZING_FIXED(key->rect.w),
                        CLAY_SIZING_FIXED(key->rect.h)
                    },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                },
                .backgroundColor = tb_ui_keyboard_key_color(active, pressed),
                .cornerRadius = CLAY_CORNER_RADIUS(tb_ui_scale_value(model, 12)),
                .border = {
                    .color = tb_ui_keyboard_key_border(active, pressed),
                    .width = {1, 1, 1, 1}
                }
            }) {
                CLAY_TEXT(tb_ui_string(model->keyboard_labels[index]), key_text);
            }
        }
    }
}
