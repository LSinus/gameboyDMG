#include <SDL2/SDL.h>
#include <SDL2/SDL_render.h>
#include <assert.h>
#include "renderer.h"
#include <SDL_ttf.h>
#include "../../external/SDL_FontCache.h"


typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    FC_Font      *font;
    char         *font_path;
    SDL_Texture  *texture;
} r_context;

static r_context *r_ctx; 

const char button_map[256] = {
  [ SDL_BUTTON_LEFT   & 0xff ] =  MU_MOUSE_LEFT,
  [ SDL_BUTTON_RIGHT  & 0xff ] =  MU_MOUSE_RIGHT,
  [ SDL_BUTTON_MIDDLE & 0xff ] =  MU_MOUSE_MIDDLE,
};

const char key_map[256] = {
  [ SDLK_LSHIFT       & 0xff ] = MU_KEY_SHIFT,
  [ SDLK_RSHIFT       & 0xff ] = MU_KEY_SHIFT,
  [ SDLK_LCTRL        & 0xff ] = MU_KEY_CTRL,
  [ SDLK_RCTRL        & 0xff ] = MU_KEY_CTRL,
  [ SDLK_LALT         & 0xff ] = MU_KEY_ALT,
  [ SDLK_RALT         & 0xff ] = MU_KEY_ALT,
  [ SDLK_RETURN       & 0xff ] = MU_KEY_RETURN,
  [ SDLK_BACKSPACE    & 0xff ] = MU_KEY_BACKSPACE,
};

static const char * codepoints_map[5] = { "\u1000", "\u2715", "\u2713", "\u25B6", "\u25BC"};

void r_init(const char* window_title, int window_width, int window_height, const char *font_path) {
    /* heap allocated to easly save and resotre on hotreload */
    r_ctx = malloc(sizeof(*r_ctx));

    if(!r_ctx) {
        fprintf(stderr, "[RENDERER] Error: buy more memory \n");
    }
    /* init SDL window */
    int init = SDL_Init(SDL_INIT_EVERYTHING);
    if(init != 0){
        fprintf(stderr, "[RENDERER] Error: %s\n", SDL_GetError());
        SDL_Quit();
    }
    r_ctx->window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height,SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if(!r_ctx->window){
        fprintf(stderr, "[RENDERER] Error: window not initialized properly: %s\n", SDL_GetError());
        SDL_Quit();
    }
    printf("[RENDERER] Window intialized\n");
    r_ctx->renderer = SDL_CreateRenderer(r_ctx->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!r_ctx->renderer){
        fprintf(stderr, "[RENDERER] Error: renderer not initialized properly\n");
        SDL_Quit();
    }
    r_ctx->font = FC_CreateFont();  
    r_ctx->font_path = font_path;
    if(FC_LoadFont(r_ctx->font, r_ctx->renderer, font_path, 12, FC_MakeColor(255,255,255,255), TTF_STYLE_NORMAL) == 0){
        exit(1);
    };

#ifndef DEBUGGER_MODE
    r_ctx->texture = SDL_CreateTexture(r_ctx->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, window_width, window_height);
#endif
}

void *r_pre_reload() {
   return r_ctx; 
}

void r_post_reload(void *state) {
    r_ctx = (r_context *)state;
    r_ctx->font = FC_CreateFont();  
    if(FC_LoadFont(r_ctx->font, r_ctx->renderer, r_ctx->font_path, 12, FC_MakeColor(255,255,255,255), TTF_STYLE_NORMAL) == 0){
        exit(1);
    };
}

void r_draw_rect(mu_Rect rect, mu_Color color) {
  SDL_SetRenderDrawColor(r_ctx->renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawRect(r_ctx->renderer, (SDL_Rect *)&rect);
  SDL_RenderFillRect(r_ctx->renderer, (SDL_Rect *)&rect);
  SDL_SetRenderDrawColor(r_ctx->renderer, 0, 0, 0, 255);
}

void r_draw_image(mu_Rect dst_rect, int img_width, int img_height, const uint32_t *framebuffer) {
#ifdef DEBUGGER_MODE
  r_ctx->texture = SDL_CreateTexture(r_ctx->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, img_width, img_height);
  SDL_UpdateTexture(r_ctx->texture, NULL, framebuffer, img_width * sizeof(uint32_t));  // Update the texture with the new pixel data
  SDL_RenderCopy(r_ctx->renderer, r_ctx->texture, NULL, (SDL_Rect *)&dst_rect); // Copy the texture to the renderer
  SDL_DestroyTexture(r_ctx->texture);
#else
  SDL_UpdateTexture(r_ctx->texture, NULL, framebuffer, img_width * sizeof(uint32_t));  // Update the texture with the new pixel data
  SDL_RenderCopy(r_ctx->renderer, r_ctx->texture, NULL, NULL);
#endif
}

void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color) {
  FC_DrawColor(r_ctx->font, r_ctx->renderer, pos.x, pos.y, *(SDL_Color*)&color, text); 
}


void r_draw_icon(int id, mu_Rect rect, mu_Color color) {
  FC_DrawColor(r_ctx->font, r_ctx->renderer, rect.x + 2, rect.y + 2, *(SDL_Color*)&color, "%s", codepoints_map[id]);
}


int r_get_text_width(const char *text, int len) {
  return FC_GetWidth(r_ctx->font, text);
}


int r_get_text_height(void) {
  return FC_GetHeight(r_ctx->font, "text");
}


void r_set_clip_rect(mu_Rect rect) {
  SDL_RenderSetClipRect(r_ctx->renderer, (SDL_Rect *)&rect);
}


void r_clear(mu_Color color) {
  SDL_SetRenderDrawColor(r_ctx->renderer, color.r, color.g, color.b, color.a);
  SDL_RenderClear(r_ctx->renderer);
}


void r_present(void) {
  SDL_RenderPresent(r_ctx->renderer);
}

void r_quit(void){
  FC_FreeFont(r_ctx->font);
  SDL_DestroyTexture(r_ctx->texture);
  SDL_DestroyRenderer(r_ctx->renderer);
  SDL_DestroyWindow(r_ctx->window);
  SDL_Quit();
}
