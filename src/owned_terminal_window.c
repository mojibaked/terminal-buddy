#include "owned_terminal_window.h"

#include <stdlib.h>
#include <string.h>

#ifdef TB_HAVE_OWNED_TERMINAL_WINDOW

#include <SDL3/SDL_system.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "renderer_gpu.h"
#include "winterm_embed.h"

#include "font_jetbrains_mono.h"

#define TB_OWNED_TERMINAL_DEFAULT_WIDTH 1280
#define TB_OWNED_TERMINAL_DEFAULT_HEIGHT 820
#define TB_OWNED_TERMINAL_MIN_WIDTH 640
#define TB_OWNED_TERMINAL_MIN_HEIGHT 420
#define TB_OWNED_TERMINAL_TITLE_MAX 256

struct TbOwnedTerminalWindow {
    SDL_Window *window;
    Uint32 window_id;
    WintermGpuRenderer *renderer;
    WintermEmbed *embed;
    TTF_Font *font;
    int pixel_width;
    int pixel_height;
    int cell_width;
    int cell_height;
    int terminal_pad;
    float display_scale;
    bool first_frame;
    bool needs_render;
    bool ttf_initialized_here;
    char title[TB_OWNED_TERMINAL_TITLE_MAX];
};

static float tb_owned_terminal_resolved_scale(float display_scale) {
    return display_scale > 0.0f ? display_scale : 1.0f;
}

static bool tb_owned_terminal_sync_pixel_size(
    TbOwnedTerminalWindow *window,
    bool *out_changed
) {
    int pixel_width = 0;
    int pixel_height = 0;

    if (out_changed != NULL) {
        *out_changed = false;
    }
    if (window == NULL || window->window == NULL) {
        return false;
    }

    if (!SDL_GetWindowSizeInPixels(window->window, &pixel_width, &pixel_height)) {
        SDL_GetWindowSize(window->window, &pixel_width, &pixel_height);
    }
    if (pixel_width < 1) {
        pixel_width = TB_OWNED_TERMINAL_DEFAULT_WIDTH;
    }
    if (pixel_height < 1) {
        pixel_height = TB_OWNED_TERMINAL_DEFAULT_HEIGHT;
    }

    if (pixel_width != window->pixel_width || pixel_height != window->pixel_height) {
        window->pixel_width = pixel_width;
        window->pixel_height = pixel_height;
        if (out_changed != NULL) {
            *out_changed = true;
        }
    }

    return true;
}

static bool tb_owned_terminal_load_font(TbOwnedTerminalWindow *window) {
    SDL_IOStream *font_io = NULL;
    TTF_Font *font = NULL;
    float display_scale = 1.0f;
    int cell_width = 0;
    int glyph_height = 0;

    if (window == NULL || window->window == NULL) {
        return false;
    }

    if (TTF_WasInit() == 0) {
        if (!TTF_Init()) {
            return false;
        }
        window->ttf_initialized_here = true;
    }

    display_scale = tb_owned_terminal_resolved_scale(
        SDL_GetWindowDisplayScale(window->window)
    );
    font_io = SDL_IOFromConstMem(font_jetbrains_mono, sizeof(font_jetbrains_mono));
    if (font_io == NULL) {
        return false;
    }

    font = TTF_OpenFontIO(font_io, true, 16.0f * display_scale);
    if (font == NULL) {
        return false;
    }
    if (!TTF_GetStringSize(font, "M", 1, &cell_width, &glyph_height)) {
        TTF_CloseFont(font);
        return false;
    }

    window->font = font;
    window->display_scale = display_scale;
    window->cell_width = cell_width > 0 ? cell_width : 1;
    window->cell_height = TTF_GetFontHeight(font);
    if (window->cell_height < 1) {
        window->cell_height = glyph_height > 0 ? glyph_height : 1;
    }
    window->terminal_pad = (int) SDL_ceilf(4.0f * display_scale);
    if (window->terminal_pad < 1) {
        window->terminal_pad = 1;
    }

    return true;
}

static bool tb_owned_terminal_set_clipboard_text(void *userdata, const char *text) {
    (void) userdata;
    return SDL_SetClipboardText(text != NULL ? text : "");
}

static char *tb_owned_terminal_get_clipboard_text(void *userdata) {
    (void) userdata;
    return SDL_GetClipboardText();
}

static void tb_owned_terminal_set_window_title(void *userdata, const char *title) {
    TbOwnedTerminalWindow *window = (TbOwnedTerminalWindow *) userdata;

    if (window == NULL) {
        return;
    }

    if (title != NULL && title[0] != '\0') {
        SDL_snprintf(window->title, sizeof(window->title), "Buddy Terminal - %s", title);
    } else {
        SDL_snprintf(window->title, sizeof(window->title), "%s", "Buddy Terminal");
    }

    if (window->window != NULL) {
        SDL_SetWindowTitle(window->window, window->title);
    }
}

static void tb_owned_terminal_set_mouse_capture(void *userdata, bool captured) {
    TbOwnedTerminalWindow *window = (TbOwnedTerminalWindow *) userdata;

    if (window == NULL || window->window == NULL) {
        return;
    }

    SDL_SetWindowMouseGrab(window->window, captured);
}

static bool tb_owned_terminal_create_runtime(TbOwnedTerminalWindow *window) {
    WintermEmbedHostCallbacks host_callbacks;

    if (window == NULL || window->window == NULL) {
        return false;
    }
    if (!tb_owned_terminal_sync_pixel_size(window, NULL)) {
        return false;
    }
    if (!tb_owned_terminal_load_font(window)) {
        return false;
    }

    window->renderer = renderer_gpu_create(window->window);
    if (window->renderer == NULL) {
        return false;
    }

    SDL_zero(host_callbacks);
    host_callbacks.userdata = window;
    host_callbacks.set_clipboard_text = tb_owned_terminal_set_clipboard_text;
    host_callbacks.get_clipboard_text = tb_owned_terminal_get_clipboard_text;
    host_callbacks.set_window_title = tb_owned_terminal_set_window_title;
    host_callbacks.set_mouse_capture = tb_owned_terminal_set_mouse_capture;

    window->embed = winterm_embed_create(
        window->renderer,
        window->font,
        window->pixel_width,
        window->pixel_height,
        window->cell_width,
        window->cell_height,
        window->terminal_pad,
        &host_callbacks
    );
    if (window->embed == NULL) {
        return false;
    }

    window->first_frame = true;
    window->needs_render = true;
    tb_owned_terminal_set_window_title(window, NULL);
    return true;
}

static void tb_owned_terminal_destroy_runtime(TbOwnedTerminalWindow *window) {
    if (window == NULL) {
        return;
    }

    if (window->embed != NULL) {
        winterm_embed_destroy(window->embed);
        window->embed = NULL;
    }
    if (window->renderer != NULL) {
        renderer_gpu_destroy(window->renderer);
        window->renderer = NULL;
    }
    if (window->font != NULL) {
        TTF_CloseFont(window->font);
        window->font = NULL;
    }
    if (window->ttf_initialized_here) {
        TTF_Quit();
        window->ttf_initialized_here = false;
    }

    window->pixel_width = 0;
    window->pixel_height = 0;
    window->cell_width = 0;
    window->cell_height = 0;
    window->terminal_pad = 0;
    window->display_scale = 1.0f;
    window->first_frame = true;
    window->needs_render = false;
    SDL_snprintf(window->title, sizeof(window->title), "%s", "Buddy Terminal");
}

static bool tb_owned_terminal_ensure_window(TbOwnedTerminalWindow *window) {
    if (window == NULL) {
        return false;
    }
    if (window->window != NULL && window->embed != NULL) {
        return true;
    }

    if (window->window == NULL) {
        window->window = SDL_CreateWindow(
            "Buddy Terminal",
            TB_OWNED_TERMINAL_DEFAULT_WIDTH,
            TB_OWNED_TERMINAL_DEFAULT_HEIGHT,
            SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
        );
        if (window->window == NULL) {
            return false;
        }

        SDL_SetWindowMinimumSize(
            window->window,
            TB_OWNED_TERMINAL_MIN_WIDTH,
            TB_OWNED_TERMINAL_MIN_HEIGHT
        );
        window->window_id = SDL_GetWindowID(window->window);
    }

    if (window->embed == NULL && !tb_owned_terminal_create_runtime(window)) {
        if (window->window != NULL) {
            SDL_DestroyWindow(window->window);
            window->window = NULL;
            window->window_id = 0;
        }
        tb_owned_terminal_destroy_runtime(window);
        return false;
    }

    return true;
}

static Uint32 tb_owned_terminal_event_window_id(const SDL_Event *event) {
    if (event == NULL) {
        return 0;
    }

    switch (event->type) {
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_HIT_TEST:
        case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        case SDL_EVENT_WINDOW_OCCLUDED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        case SDL_EVENT_WINDOW_DESTROYED:
        case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
            return event->window.windowID;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return event->key.windowID;
        case SDL_EVENT_TEXT_INPUT:
            return event->text.windowID;
        case SDL_EVENT_MOUSE_MOTION:
            return event->motion.windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return event->button.windowID;
        case SDL_EVENT_MOUSE_WHEEL:
            return event->wheel.windowID;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED:
            return event->tfinger.windowID;
        default:
            return 0;
    }
}

static void tb_owned_terminal_pointer_scale(
    TbOwnedTerminalWindow *window,
    double logical_x,
    double logical_y,
    double *out_pixel_x,
    double *out_pixel_y,
    int *out_window_width_px,
    int *out_window_height_px
) {
    int logical_width = 0;
    int logical_height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    double scale_x = 1.0;
    double scale_y = 1.0;

    if (out_pixel_x != NULL) {
        *out_pixel_x = logical_x;
    }
    if (out_pixel_y != NULL) {
        *out_pixel_y = logical_y;
    }
    if (out_window_width_px != NULL) {
        *out_window_width_px = window != NULL ? window->pixel_width : 0;
    }
    if (out_window_height_px != NULL) {
        *out_window_height_px = window != NULL ? window->pixel_height : 0;
    }
    if (window == NULL || window->window == NULL) {
        return;
    }

    SDL_GetWindowSize(window->window, &logical_width, &logical_height);
    if (!SDL_GetWindowSizeInPixels(window->window, &pixel_width, &pixel_height)) {
        pixel_width = logical_width;
        pixel_height = logical_height;
    }
    if (logical_width > 0 && pixel_width > 0) {
        scale_x = (double) pixel_width / (double) logical_width;
    }
    if (logical_height > 0 && pixel_height > 0) {
        scale_y = (double) pixel_height / (double) logical_height;
    }

    if (out_pixel_x != NULL) {
        *out_pixel_x = logical_x * scale_x;
    }
    if (out_pixel_y != NULL) {
        *out_pixel_y = logical_y * scale_y;
    }
    if (out_window_width_px != NULL) {
        *out_window_width_px = pixel_width;
    }
    if (out_window_height_px != NULL) {
        *out_window_height_px = pixel_height;
    }
}

static bool tb_owned_terminal_send_key(
    TbOwnedTerminalWindow *window,
    SDL_Scancode scancode,
    SDL_Keycode keycode
) {
    WintermKeyInput press;
    WintermKeyInput release;

    if (window == NULL || window->embed == NULL) {
        return false;
    }

    SDL_zero(press);
    press.scancode = scancode;
    press.keycode = keycode;
    press.mods = SDL_KMOD_NONE;
    press.down = true;
    press.repeat = false;

    release = press;
    release.down = false;

    winterm_embed_handle_key(window->embed, &press);
    winterm_embed_handle_key(window->embed, &release);
    window->needs_render = true;
    return true;
}

TbOwnedTerminalWindow *tb_owned_terminal_window_create(void) {
    TbOwnedTerminalWindow *window = (TbOwnedTerminalWindow *) SDL_calloc(1, sizeof(*window));

    if (window != NULL) {
        SDL_snprintf(window->title, sizeof(window->title), "%s", "Buddy Terminal");
        window->first_frame = true;
        window->display_scale = 1.0f;
    }
    return window;
}

void tb_owned_terminal_window_destroy(TbOwnedTerminalWindow *window) {
    if (window == NULL) {
        return;
    }

    tb_owned_terminal_destroy_runtime(window);
    if (window->window != NULL) {
        SDL_DestroyWindow(window->window);
        window->window = NULL;
    }
    SDL_free(window);
}

bool tb_owned_terminal_window_is_available(void) {
    return true;
}

bool tb_owned_terminal_window_show(TbOwnedTerminalWindow *window) {
    if (!tb_owned_terminal_ensure_window(window)) {
        return false;
    }

    SDL_ShowWindow(window->window);
    SDL_RaiseWindow(window->window);
    SDL_SyncWindow(window->window);
    SDL_StartTextInput(window->window);
    winterm_embed_handle_focus(window->embed, true);
    window->needs_render = true;
    return true;
}

bool tb_owned_terminal_window_focus(TbOwnedTerminalWindow *window) {
    if (window == NULL || window->window == NULL) {
        return false;
    }

    if (SDL_GetWindowFlags(window->window) & SDL_WINDOW_HIDDEN) {
        SDL_ShowWindow(window->window);
    }
    SDL_RaiseWindow(window->window);
    SDL_SyncWindow(window->window);
    SDL_StartTextInput(window->window);
    window->needs_render = true;
    return true;
}

bool tb_owned_terminal_window_has_window(const TbOwnedTerminalWindow *window) {
    return window != NULL && window->window != NULL;
}

bool tb_owned_terminal_window_is_visible(const TbOwnedTerminalWindow *window) {
    return window != NULL
        && window->window != NULL
        && (SDL_GetWindowFlags(window->window) & SDL_WINDOW_HIDDEN) == 0;
}

bool tb_owned_terminal_window_handles_event(
    TbOwnedTerminalWindow *window,
    const SDL_Event *event
) {
    Uint32 event_window_id = 0;

    if (window == NULL || window->window == NULL || window->embed == NULL || event == NULL) {
        return false;
    }

    event_window_id = tb_owned_terminal_event_window_id(event);
    if (event_window_id == 0 || event_window_id != window->window_id) {
        return false;
    }

    switch (event->type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            winterm_embed_handle_focus(window->embed, false);
            SDL_StopTextInput(window->window);
            SDL_HideWindow(window->window);
            window->needs_render = true;
            return true;
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            window->needs_render = true;
            return true;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            SDL_StartTextInput(window->window);
            winterm_embed_handle_focus(window->embed, true);
            window->needs_render = true;
            return true;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            winterm_embed_handle_focus(window->embed, false);
            window->needs_render = true;
            return true;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            WintermKeyInput input;

            SDL_zero(input);
            input.scancode = event->key.scancode;
            input.keycode = event->key.key;
            input.mods = event->key.mod;
            input.raw = event->key.raw;
            input.down = event->type == SDL_EVENT_KEY_DOWN;
            input.repeat = event->key.repeat;
            winterm_embed_handle_key(window->embed, &input);
            window->needs_render = true;
            return true;
        }
        case SDL_EVENT_TEXT_INPUT:
            winterm_embed_queue_text_input(window->embed, event->text.text);
            window->needs_render = true;
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            WintermMouseButtonInput input;

            SDL_zero(input);
            tb_owned_terminal_pointer_scale(
                window,
                event->button.x,
                event->button.y,
                &input.pointer.x,
                &input.pointer.y,
                &input.pointer.window_width_px,
                &input.pointer.window_height_px
            );
            input.pointer.mods = SDL_GetModState();
            input.pointer.buttons = SDL_GetMouseState(NULL, NULL);
            input.button = event->button.button;
            input.clicks = event->button.clicks;
            input.down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            winterm_embed_handle_mouse_button(window->embed, &input);
            window->needs_render = true;
            return true;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            WintermMouseMotionInput input;

            SDL_zero(input);
            tb_owned_terminal_pointer_scale(
                window,
                event->motion.x,
                event->motion.y,
                &input.pointer.x,
                &input.pointer.y,
                &input.pointer.window_width_px,
                &input.pointer.window_height_px
            );
            input.pointer.mods = SDL_GetModState();
            input.pointer.buttons = event->motion.state;
            winterm_embed_handle_mouse_motion(window->embed, &input);
            window->needs_render = true;
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            WintermMouseWheelInput input;
            float delta_x = event->wheel.x;
            float delta_y = event->wheel.y;

            SDL_zero(input);
            tb_owned_terminal_pointer_scale(
                window,
                event->wheel.mouse_x,
                event->wheel.mouse_y,
                &input.pointer.x,
                &input.pointer.y,
                &input.pointer.window_width_px,
                &input.pointer.window_height_px
            );
            if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                delta_x *= -1.0f;
                delta_y *= -1.0f;
            }
            input.pointer.mods = SDL_GetModState();
            input.pointer.buttons = SDL_GetMouseState(NULL, NULL);
            input.delta_x = delta_x;
            input.delta_y = delta_y;
            winterm_embed_handle_mouse_wheel(window->embed, &input);
            window->needs_render = true;
            return true;
        }
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED: {
            WintermTouchInput input;

            SDL_zero(input);
            input.touch_id = event->tfinger.touchID;
            input.finger_id = event->tfinger.fingerID;
            input.x_px = event->tfinger.x * (double) window->pixel_width;
            input.y_px = event->tfinger.y * (double) window->pixel_height;
            input.timestamp_ns = SDL_GetTicksNS();

            if (event->type == SDL_EVENT_FINGER_DOWN) {
                winterm_embed_handle_touch_down(window->embed, &input);
            } else if (event->type == SDL_EVENT_FINGER_MOTION) {
                winterm_embed_handle_touch_motion(window->embed, &input);
            } else {
                winterm_embed_handle_touch_up(window->embed, &input);
            }
            window->needs_render = true;
            return true;
        }
        default:
            return false;
    }
}

void tb_owned_terminal_window_tick(TbOwnedTerminalWindow *window) {
    bool window_resized = false;
    bool rendered = false;
    bool visible = false;
    WintermEmbedFramePlan frame_plan;
    WintermEmbedRenderData render_data;
    WintermSpeechEvent speech_event;

    if (window == NULL || window->window == NULL || window->embed == NULL) {
        return;
    }

    tb_owned_terminal_sync_pixel_size(window, &window_resized);
    visible = tb_owned_terminal_window_is_visible(window);

    SDL_zero(frame_plan);
    if (!winterm_embed_begin_frame(window->embed, window_resized, window->first_frame, &frame_plan)) {
        return;
    }

    while (winterm_embed_take_speech_event(window->embed, &speech_event)) {
    }

    if (frame_plan.quit_requested) {
        winterm_embed_end_frame(window->embed, false);
        tb_owned_terminal_destroy_runtime(window);
        if (window->window != NULL) {
            SDL_DestroyWindow(window->window);
            window->window = NULL;
            window->window_id = 0;
        }
        return;
    }

    if (visible && (window->needs_render || frame_plan.should_render || window->first_frame)) {
        SDL_zero(render_data);
        if (
            winterm_embed_build_frame(window->embed, &render_data)
            && renderer_gpu_render(
                window->renderer,
                render_data.background_color,
                window->pixel_width,
                window->pixel_height,
                render_data.batches,
                render_data.batch_count
            )
        ) {
            rendered = true;
        }
    }

    winterm_embed_end_frame(window->embed, rendered);
    window->first_frame = false;
    window->needs_render = !rendered;
}

bool tb_owned_terminal_window_submit_text(
    TbOwnedTerminalWindow *window,
    const char *text,
    bool submit
) {
    if (!tb_owned_terminal_ensure_window(window) || text == NULL || text[0] == '\0') {
        return false;
    }

    winterm_embed_queue_text_input(window->embed, text);
    window->needs_render = true;
    if (submit) {
        return tb_owned_terminal_window_send_enter(window);
    }
    return true;
}

bool tb_owned_terminal_window_send_enter(TbOwnedTerminalWindow *window) {
    return tb_owned_terminal_send_key(window, SDL_SCANCODE_RETURN, SDLK_RETURN);
}

void *tb_owned_terminal_window_native_handle(const TbOwnedTerminalWindow *window) {
    SDL_PropertiesID props = 0;

    if (window == NULL || window->window == NULL) {
        return NULL;
    }

    props = SDL_GetWindowProperties(window->window);
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

const char *tb_owned_terminal_window_title(const TbOwnedTerminalWindow *window) {
    return (window != NULL && window->title[0] != '\0') ? window->title : "Buddy Terminal";
}

#else

struct TbOwnedTerminalWindow {
    int unused;
};

TbOwnedTerminalWindow *tb_owned_terminal_window_create(void) {
    return (TbOwnedTerminalWindow *) SDL_calloc(1, sizeof(TbOwnedTerminalWindow));
}

void tb_owned_terminal_window_destroy(TbOwnedTerminalWindow *window) {
    SDL_free(window);
}

bool tb_owned_terminal_window_is_available(void) {
    return false;
}

bool tb_owned_terminal_window_show(TbOwnedTerminalWindow *window) {
    (void) window;
    return false;
}

bool tb_owned_terminal_window_focus(TbOwnedTerminalWindow *window) {
    (void) window;
    return false;
}

bool tb_owned_terminal_window_has_window(const TbOwnedTerminalWindow *window) {
    (void) window;
    return false;
}

bool tb_owned_terminal_window_is_visible(const TbOwnedTerminalWindow *window) {
    (void) window;
    return false;
}

bool tb_owned_terminal_window_handles_event(
    TbOwnedTerminalWindow *window,
    const SDL_Event *event
) {
    (void) window;
    (void) event;
    return false;
}

void tb_owned_terminal_window_tick(TbOwnedTerminalWindow *window) {
    (void) window;
}

bool tb_owned_terminal_window_submit_text(
    TbOwnedTerminalWindow *window,
    const char *text,
    bool submit
) {
    (void) window;
    (void) text;
    (void) submit;
    return false;
}

bool tb_owned_terminal_window_send_enter(TbOwnedTerminalWindow *window) {
    (void) window;
    return false;
}

void *tb_owned_terminal_window_native_handle(const TbOwnedTerminalWindow *window) {
    (void) window;
    return NULL;
}

const char *tb_owned_terminal_window_title(const TbOwnedTerminalWindow *window) {
    (void) window;
    return "Buddy Terminal";
}

#endif
