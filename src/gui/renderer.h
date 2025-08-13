#ifndef RENDERER_H
#define RENDERER_H

#include "microui.h"
#include <stdint.h>

extern const char button_map[256];
extern const char key_map[256];

void r_init(const char* window_title, int window_width, int window_height, const char *font_path);
void r_draw_rect(mu_Rect rect, mu_Color color);
void r_draw_image(mu_Rect dst_rect, int img_width, int img_height, const uint32_t *framebuffer);
void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color);
void r_draw_icon(int id, mu_Rect rect, mu_Color color);
 int r_get_text_width(const char *text, int len);
 int r_get_text_height(void);
void r_set_clip_rect(mu_Rect rect);
void r_clear(mu_Color color);
void r_present(void);
void r_quit(void);

#endif