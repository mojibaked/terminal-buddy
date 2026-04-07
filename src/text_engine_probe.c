#include <stdbool.h>
#include <stdio.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

static TTF_Font *open_probe_font(void) {
    static const char *candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf"
    };

    for (int i = 0; i < (int) SDL_arraysize(candidates); ++i) {
        TTF_Font *font = TTF_OpenFont(candidates[i], 24.0f);
        if (font != NULL) {
            SDL_Log("probe: opened font %s", candidates[i]);
            return font;
        }
    }

    return NULL;
}

static bool draw_surface_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, float x, float y) {
    SDL_Color color = {255, 255, 255, 255};
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, 0, color);
    SDL_Texture *texture = NULL;
    SDL_FRect dest;

    if (surface == NULL) {
        SDL_Log("probe: TTF_RenderText_Blended failed: %s", SDL_GetError());
        return false;
    }

    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == NULL) {
        SDL_Log("probe: SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
        SDL_DestroySurface(surface);
        return false;
    }

    dest.x = x;
    dest.y = y;
    dest.w = (float) surface->w;
    dest.h = (float) surface->h;
    SDL_RenderTexture(renderer, texture, NULL, &dest);
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
    return true;
}

int main(void) {
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    TTF_TextEngine *text_engine = NULL;
    TTF_Font *font = NULL;
    TTF_Text *engine_text = NULL;
    SDL_Surface *capture = NULL;
    const char *renderer_name = NULL;
    SDL_Rect viewport;
    SDL_Rect clip_rect;
    int output_w = 0;
    int output_h = 0;
    bool engine_ok = false;
    bool surface_ok = false;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("probe: SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    if (!TTF_Init()) {
        SDL_Log("probe: TTF_Init failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    window = SDL_CreateWindow(
        "terminal-buddy text probe",
        860,
        260,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_TRANSPARENT
    );
    if (window == NULL) {
        SDL_Log("probe: SDL_CreateWindow failed: %s", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, "direct3d11");
    if (renderer == NULL) {
        SDL_Log("probe: SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    renderer_name = SDL_GetRendererName(renderer);
    SDL_zero(viewport);
    SDL_zero(clip_rect);
    SDL_GetCurrentRenderOutputSize(renderer, &output_w, &output_h);
    SDL_GetRenderViewport(renderer, &viewport);
    SDL_GetRenderClipRect(renderer, &clip_rect);
    SDL_Log(
        "probe: renderer=%s output=%dx%d viewport=%d,%d %dx%d clip=%d,%d %dx%d",
        renderer_name ? renderer_name : "(null)",
        output_w,
        output_h,
        viewport.x,
        viewport.y,
        viewport.w,
        viewport.h,
        clip_rect.x,
        clip_rect.y,
        clip_rect.w,
        clip_rect.h
    );

    font = open_probe_font();
    if (font == NULL) {
        SDL_Log("probe: failed to load font: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    text_engine = TTF_CreateRendererTextEngine(renderer);
    if (text_engine == NULL) {
        SDL_Log("probe: TTF_CreateRendererTextEngine failed: %s", SDL_GetError());
        TTF_CloseFont(font);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    engine_text = TTF_CreateText(text_engine, font, "TTF_DrawRendererText path", 0);
    if (engine_text == NULL) {
        SDL_Log("probe: TTF_CreateText failed: %s", SDL_GetError());
    } else {
        TTF_SetTextColor(engine_text, 255, 255, 255, 255);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 10, 14, 20, 220);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 24, 32, 48, 245);
    SDL_RenderFillRect(renderer, &(SDL_FRect){20.0f, 20.0f, 820.0f, 90.0f});
    SDL_RenderFillRect(renderer, &(SDL_FRect){20.0f, 132.0f, 820.0f, 90.0f});

    if (engine_text != NULL) {
        engine_ok = TTF_DrawRendererText(engine_text, 40.0f, 52.0f);
        if (!engine_ok) {
            SDL_Log("probe: TTF_DrawRendererText returned false: %s", SDL_GetError());
        } else {
            SDL_Log("probe: TTF_DrawRendererText returned true");
        }
    }

    surface_ok = draw_surface_text(renderer, font, "TTF_RenderText_Blended -> SDL_Texture path", 40.0f, 164.0f);
    SDL_Log("probe: surface path returned %s", surface_ok ? "true" : "false");

    SDL_RenderPresent(renderer);

    capture = SDL_RenderReadPixels(renderer, NULL);
    if (capture != NULL) {
        const char *capture_path = "C:/Users/ether/projects/terminal-buddy/build/text-engine-probe.bmp";
        if (SDL_SaveBMP(capture, capture_path)) {
            SDL_Log("probe: saved %s", capture_path);
        } else {
            SDL_Log("probe: SDL_SaveBMP failed: %s", SDL_GetError());
        }
        SDL_DestroySurface(capture);
    } else {
        SDL_Log("probe: SDL_RenderReadPixels failed: %s", SDL_GetError());
    }

    SDL_Delay(1200);

    if (engine_text != NULL) {
        TTF_DestroyText(engine_text);
    }
    TTF_DestroyRendererTextEngine(text_engine);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
