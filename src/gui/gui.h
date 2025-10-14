#ifndef GUI_H
#define GUI_H

#include "../../external/microui.h"
#include "renderer.h"

extern mu_Context ctx;
extern float bg[3];
extern uint32_t framebuffer[USER_WINDOW_HEIGHT][USER_WINDOW_WIDTH];
extern uint32_t tiledata[USER_WINDOW_HEIGHT][USER_WINDOW_WIDTH];

int gui_text_width(mu_Font font, const char *text, int len);
int gui_text_height(mu_Font font);
void gui_process_frame(mu_Context *ctx);


#endif
