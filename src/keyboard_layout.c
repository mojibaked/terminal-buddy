#include "keyboard_layout.h"

#include <stdio.h>

#define TB_KEYBOARD_SHELL_WIDTH 968.0f
#define TB_KEYBOARD_SHELL_HEIGHT 378.0f
#define TB_KEYBOARD_SHELL_RADIUS 34.0f
#define TB_KEYBOARD_PADDING 14.0f
#define TB_KEYBOARD_RAIL_WIDTH 88.0f
#define TB_KEYBOARD_CONTENT_GAP 16.0f
#define TB_KEYBOARD_HEADER_HEIGHT 22.0f
#define TB_KEYBOARD_SUBTITLE_HEIGHT 18.0f
#define TB_KEYBOARD_MODIFIER_HEIGHT 22.0f
#define TB_KEYBOARD_ROW_GAP 8.0f
#define TB_KEYBOARD_COLUMN_GAP 8.0f
#define TB_KEYBOARD_KEY_HEIGHT 38.0f
#define TB_KEYBOARD_BUTTON_SIZE 76.0f

static const TbKeyboardKeySpec g_nav_row[] = {
    {"esc", "Esc", NULL, NULL, TB_KEYBOARD_ACTION_ESCAPE, 1.0f, false},
    {"home", "Home", NULL, NULL, TB_KEYBOARD_ACTION_HOME, 1.0f, true},
    {"end", "End", NULL, NULL, TB_KEYBOARD_ACTION_END, 1.0f, true},
    {"pgup", "PgUp", NULL, NULL, TB_KEYBOARD_ACTION_PAGE_UP, 1.0f, true},
    {"pgdn", "PgDn", NULL, NULL, TB_KEYBOARD_ACTION_PAGE_DOWN, 1.0f, true},
    {"backspace", "Bksp", NULL, NULL, TB_KEYBOARD_ACTION_BACKSPACE, 1.5f, true}
};

static const TbKeyboardKeySpec g_number_row[] = {
    {"1", "1", "!", "1", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"2", "2", "@", "2", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"3", "3", "#", "3", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"4", "4", "$", "4", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"5", "5", "%", "5", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"6", "6", "^", "6", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"7", "7", "&", "7", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"8", "8", "*", "8", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"9", "9", "(", "9", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"0", "0", ")", "0", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"dash", "-", "_", "-", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"slash", "/", "?", "/", TB_KEYBOARD_ACTION_CHAR, 1.0f, false}
};

static const TbKeyboardKeySpec g_qwerty_row[] = {
    {"tab", "Tab", NULL, NULL, TB_KEYBOARD_ACTION_TAB, 1.35f, false},
    {"q", "q", NULL, "q", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"w", "w", NULL, "w", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"e", "e", NULL, "e", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"r", "r", NULL, "r", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"t", "t", NULL, "t", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"y", "y", NULL, "y", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"u", "u", NULL, "u", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"i", "i", NULL, "i", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"o", "o", NULL, "o", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"p", "p", NULL, "p", TB_KEYBOARD_ACTION_CHAR, 1.0f, false}
};

static const TbKeyboardKeySpec g_home_row[] = {
    {"ctrl", "Ctrl", NULL, NULL, TB_KEYBOARD_ACTION_CTRL, 1.35f, false},
    {"a", "a", NULL, "a", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"s", "s", NULL, "s", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"d", "d", NULL, "d", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"f", "f", NULL, "f", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"g", "g", NULL, "g", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"h", "h", NULL, "h", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"j", "j", NULL, "j", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"k", "k", NULL, "k", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"l", "l", NULL, "l", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"semicolon", ";", ":", ";", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"quote", "'", "\"", "'", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"enter", "Enter", NULL, NULL, TB_KEYBOARD_ACTION_ENTER, 1.75f, false}
};

static const TbKeyboardKeySpec g_symbol_row[] = {
    {"shift", "Shift", NULL, NULL, TB_KEYBOARD_ACTION_SHIFT, 1.6f, false},
    {"z", "z", NULL, "z", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"x", "x", NULL, "x", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"c", "c", NULL, "c", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"v", "v", NULL, "v", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"b", "b", NULL, "b", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"n", "n", NULL, "n", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"m", "m", NULL, "m", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"comma", ",", "<", ",", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"period", ".", ">", ".", TB_KEYBOARD_ACTION_CHAR, 1.0f, false},
    {"up", "Up", NULL, NULL, TB_KEYBOARD_ACTION_UP, 1.2f, true}
};

static const TbKeyboardKeySpec g_space_row[] = {
    {"ctrl2", "Ctrl", NULL, NULL, TB_KEYBOARD_ACTION_CTRL, 1.3f, false},
    {"alt", "Alt", NULL, NULL, TB_KEYBOARD_ACTION_ALT, 1.1f, false},
    {"space", "Space", NULL, " ", TB_KEYBOARD_ACTION_SPACE, 4.8f, false},
    {"left", "Left", NULL, NULL, TB_KEYBOARD_ACTION_LEFT, 1.0f, true},
    {"down", "Down", NULL, NULL, TB_KEYBOARD_ACTION_DOWN, 1.0f, true},
    {"right", "Right", NULL, NULL, TB_KEYBOARD_ACTION_RIGHT, 1.0f, true}
};

typedef struct TbKeyboardRow {
    const TbKeyboardKeySpec *keys;
    int count;
} TbKeyboardRow;

static const TbKeyboardRow g_rows[] = {
    {g_nav_row, (int) SDL_arraysize(g_nav_row)},
    {g_number_row, (int) SDL_arraysize(g_number_row)},
    {g_qwerty_row, (int) SDL_arraysize(g_qwerty_row)},
    {g_home_row, (int) SDL_arraysize(g_home_row)},
    {g_symbol_row, (int) SDL_arraysize(g_symbol_row)},
    {g_space_row, (int) SDL_arraysize(g_space_row)}
};

static bool point_in_rect(const SDL_FRect *rect, float x, float y) {
    return rect != NULL
        && x >= rect->x
        && y >= rect->y
        && x <= rect->x + rect->w
        && y <= rect->y + rect->h;
}

bool tb_keyboard_build_layout(int pixel_width, int pixel_height, float ui_scale, TbKeyboardLayoutResult *out_layout) {
    float scale = ui_scale > 0.01f ? ui_scale : 1.0f;
    float shell_width = TB_KEYBOARD_SHELL_WIDTH * scale;
    float shell_height = TB_KEYBOARD_SHELL_HEIGHT * scale;
    float padding = TB_KEYBOARD_PADDING * scale;
    float rail_width = TB_KEYBOARD_RAIL_WIDTH * scale;
    float content_gap = TB_KEYBOARD_CONTENT_GAP * scale;
    float header_height = TB_KEYBOARD_HEADER_HEIGHT * scale;
    float subtitle_height = TB_KEYBOARD_SUBTITLE_HEIGHT * scale;
    float modifier_height = TB_KEYBOARD_MODIFIER_HEIGHT * scale;
    float row_gap = TB_KEYBOARD_ROW_GAP * scale;
    float column_gap = TB_KEYBOARD_COLUMN_GAP * scale;
    float key_height = TB_KEYBOARD_KEY_HEIGHT * scale;
    float button_size = TB_KEYBOARD_BUTTON_SIZE * scale;
    float shell_x = 0.0f;
    float shell_y = 0.0f;
    float content_x = 0.0f;
    float content_y = 0.0f;
    float content_width = 0.0f;
    float content_height = 0.0f;
    float keyboard_y = 0.0f;
    float keyboard_height = 0.0f;
    int key_index = 0;

    if (out_layout == NULL) {
        return false;
    }

    SDL_zero(*out_layout);

    shell_x = SDL_floorf(((float) pixel_width - shell_width) * 0.5f);
    shell_y = SDL_floorf(((float) pixel_height - shell_height) * 0.5f);

    out_layout->shell_rect = (SDL_FRect) {shell_x, shell_y, shell_width, shell_height};
    out_layout->bubble_rect = (SDL_FRect) {
        shell_x + (10.0f * scale),
        shell_y + ((shell_height - button_size) * 0.5f),
        button_size,
        button_size
    };

    content_x = shell_x + padding + rail_width + content_gap;
    content_y = shell_y + padding;
    content_width = shell_width - (padding * 2.0f) - rail_width - content_gap;
    content_height = shell_height - (padding * 2.0f);

    out_layout->header_rect = (SDL_FRect) {content_x, content_y, content_width, header_height};
    out_layout->subtitle_rect = (SDL_FRect) {content_x, content_y + header_height + (4.0f * scale), content_width, subtitle_height};
    out_layout->modifier_rect = (SDL_FRect) {
        content_x,
        content_y + header_height + subtitle_height + (10.0f * scale),
        content_width,
        modifier_height
    };

    keyboard_y = out_layout->modifier_rect.y + out_layout->modifier_rect.h + (12.0f * scale);
    keyboard_height = content_y + content_height - keyboard_y;
    out_layout->keyboard_rect = (SDL_FRect) {content_x, keyboard_y, content_width, keyboard_height};

    for (int row_index = 0; row_index < (int) SDL_arraysize(g_rows); ++row_index) {
        const TbKeyboardRow *row = &g_rows[row_index];
        float total_weight = 0.0f;
        float available_width = content_width - (column_gap * (float) (row->count - 1));
        float cursor_x = content_x;
        float row_y = keyboard_y + ((key_height + row_gap) * (float) row_index);

        for (int key = 0; key < row->count; ++key) {
            total_weight += row->keys[key].weight;
        }

        for (int key = 0; key < row->count; ++key) {
            const TbKeyboardKeySpec *spec = &row->keys[key];
            float key_width = SDL_floorf((available_width * spec->weight) / total_weight);

            if (key_index >= TB_KEYBOARD_MAX_KEYS) {
                return false;
            }

            if (key == row->count - 1) {
                key_width = (content_x + content_width) - cursor_x;
            }

            out_layout->keys[key_index].spec = spec;
            out_layout->keys[key_index].rect = (SDL_FRect) {cursor_x, row_y, key_width, key_height};
            out_layout->keys[key_index].index = key_index;
            ++out_layout->key_count;
            ++key_index;
            cursor_x += key_width + column_gap;
        }
    }

    return true;
}

const TbKeyboardKeyRect *tb_keyboard_hit_test(const TbKeyboardLayoutResult *layout, float x, float y) {
    if (layout == NULL) {
        return NULL;
    }

    for (int index = 0; index < layout->key_count; ++index) {
        if (point_in_rect(&layout->keys[index].rect, x, y)) {
            return &layout->keys[index];
        }
    }

    return NULL;
}

const TbKeyboardKeyRect *tb_keyboard_key_rect_at(const TbKeyboardLayoutResult *layout, int index) {
    if (layout == NULL || index < 0 || index >= layout->key_count) {
        return NULL;
    }

    return &layout->keys[index];
}

bool tb_keyboard_spec_is_modifier(const TbKeyboardKeySpec *spec) {
    if (spec == NULL) {
        return false;
    }

    return spec->action == TB_KEYBOARD_ACTION_SHIFT
        || spec->action == TB_KEYBOARD_ACTION_CTRL
        || spec->action == TB_KEYBOARD_ACTION_ALT;
}

bool tb_keyboard_spec_is_active(const TbKeyboardKeySpec *spec, const TbKeyboardModState *mods) {
    if (spec == NULL || mods == NULL) {
        return false;
    }

    switch (spec->action) {
        case TB_KEYBOARD_ACTION_SHIFT:
            return mods->shift || mods->caps_lock;
        case TB_KEYBOARD_ACTION_CTRL:
            return mods->ctrl;
        case TB_KEYBOARD_ACTION_ALT:
            return mods->alt;
        default:
            return false;
    }
}

const char *tb_keyboard_display_label(
    const TbKeyboardKeySpec *spec,
    const TbKeyboardModState *mods,
    char *buffer,
    size_t buffer_size
) {
    char ch = '\0';
    char shifted = '\0';

    if (spec == NULL) {
        return "";
    }

    if (spec->action != TB_KEYBOARD_ACTION_CHAR || spec->output == NULL || spec->output[0] == '\0' || spec->output[1] != '\0') {
        return spec->label != NULL ? spec->label : "";
    }

    if (buffer == NULL || buffer_size < 2) {
        return spec->label != NULL ? spec->label : "";
    }

    ch = spec->output[0];
    if (spec->shift_label != NULL && spec->shift_label[0] != '\0' && spec->shift_label[1] == '\0') {
        shifted = spec->shift_label[0];
    }
    ch = tb_keyboard_apply_shift_char(mods, ch, shifted);
    SDL_snprintf(buffer, buffer_size, "%c", ch);
    return buffer;
}
