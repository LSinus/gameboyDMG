#include <SDL2/SDL.h>
#include <SDL2/SDL_render.h>
#include <assert.h>
#include "renderer.h"
#include <SDL2/SDL_ttf.h>
#include "SDL_FontCache.h"



static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;
static FC_Font      *font;

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
  /* init SDL window */
  SDL_Init(SDL_INIT_EVERYTHING);
  window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  font = FC_CreateFont();  
  if(FC_LoadFont(font, renderer, font_path, 12, FC_MakeColor(255,255,255,255), TTF_STYLE_NORMAL) == 0){
    exit(1);
  };

  #ifndef DEBUGGER_MODE
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, window_width, window_height);
  #endif
}

void r_draw_rect(mu_Rect rect, mu_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawRect(renderer, (SDL_Rect *)&rect);
  SDL_RenderFillRect(renderer, (SDL_Rect *)&rect);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
}

void r_draw_image(mu_Rect dst_rect, int img_width, int img_height, const uint32_t *framebuffer) {
  
  #ifdef DEBUGGER_MODE
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, img_width, img_height);
  SDL_UpdateTexture(texture, NULL, framebuffer, img_width * sizeof(uint32_t));  // Update the texture with the new pixel data
  SDL_RenderCopy(renderer, texture, NULL, (SDL_Rect *)&dst_rect); // Copy the texture to the renderer
  //SDL_DestroyTexture(texture);
  #else
  SDL_UpdateTexture(texture, NULL, framebuffer, img_width * sizeof(uint32_t));  // Update the texture with the new pixel data
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  #endif
}

void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color) {
  FC_DrawColor(font, renderer, pos.x, pos.y, *(SDL_Color*)&color, text); 
}


void r_draw_icon(int id, mu_Rect rect, mu_Color color) {
  FC_DrawColor(font, renderer, rect.x + 2, rect.y + 2, *(SDL_Color*)&color, "%s", codepoints_map[id]);
}


int r_get_text_width(const char *text, int len) {
  return FC_GetWidth(font, text);
}


int r_get_text_height(void) {
  return FC_GetHeight(font, "text");
}


void r_set_clip_rect(mu_Rect rect) {
  SDL_RenderSetClipRect(renderer, (SDL_Rect *)&rect);
}


void r_clear(mu_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderClear(renderer);
}


void r_present(void) {
  SDL_RenderPresent(renderer);
}

void r_quit(void){
  FC_FreeFont(font);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}