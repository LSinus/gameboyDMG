#include <string.h>
#include <stdbool.h>

#include "microui.h"
#include "renderer.h"
#include "../hardware/device.h"

uint32_t framebuffer[USER_WINDOW_HEIGHT][USER_WINDOW_WIDTH] = {0};
uint32_t tiledata[USER_WINDOW_HEIGHT][USER_WINDOW_WIDTH] = {0};


#ifdef DEBUGGER_MODE

extern DEVICE device;

/* ---- MICROUI STUFF ---- */
mu_Context ctx = {0};
float bg[3] = { 90, 95, 100 };
static char buffer[128] = {0};


int gui_text_width(mu_Font font, const char *text, int len) {
    if (len == -1) { len = strlen(text); }
    return r_get_text_width(text, len);
}

int gui_text_height(mu_Font font) {
    return r_get_text_height();
}

static void debug_window(mu_Context *ctx) {
    if (mu_begin_window(ctx, "Debug infos", mu_rect(10, 10, 300, 450))) {
        mu_Container *win = mu_get_current_container(ctx);
        win->rect.w = mu_max(win->rect.w, 300);
        win->rect.h = mu_max(win->rect.h, 450);
        mu_layout_row(ctx, 1, (int[]) { -1 }, -100);
        mu_begin_panel(ctx, "CPU status");
        
        mu_Container *panel = mu_get_current_container(ctx);
        mu_layout_row(ctx, 1, (int[]) { -1 }, -1);
        mu_text(ctx, buffer);
        mu_end_panel(ctx);

        mu_layout_row(ctx, 2, (int[]) { 100, -1 }, -1);
        mu_layout_begin_column(ctx);
        if(mu_button(ctx, "Restart")){
            memset(device.memory, 0, 65536);
            InitializePowerOnState();
            InitializeBootROM();
            InitializeGameROM(device.romPath);
        }

        mu_layout_end_column(ctx);
        mu_layout_begin_column(ctx);

        GetEmulatorStatus(buffer);

        const char* halt_text = device.cpu.halted ? "De-Halt CPU" : "Halt CPU";
        if(mu_button(ctx, halt_text)){
            device.cpu.halted = !device.cpu.halted;
        }

        const char* slow_text = device.cpu.slowed ? "Std clock" : "Slow clock";
        if(mu_button(ctx, slow_text)){
            device.cpu.slowed = ! device.cpu.slowed;
        }
        
        mu_layout_end_column(ctx);  
        mu_end_window(ctx);
    }
}

static void joypad_window(mu_Context *ctx){
    if (mu_begin_window(ctx, "Joypad", mu_rect(10, 500, 300, 50))) {
        mu_Container *win = mu_get_current_container(ctx);
        win->rect.w = mu_max(win->rect.w, 100);
        win->rect.h = mu_max(win->rect.h, 200);
        mu_layout_row(ctx, 1, (int[]) { -1 }, -100);
        mu_layout_begin_column(ctx);
        if(mu_button(ctx, "Start")){
            device.joypad.start = true;
            device.memory[IF_REG] |= 0x10;
        }

        if(mu_button(ctx, "Select")){
            device.joypad.select = true;
            device.memory[IF_REG] |= 0x10;
        }

        if(mu_button(ctx, "Reset pressed")){
            device.joypad.select = false;
            device.joypad.start = false;
        }
        mu_layout_end_column(ctx);
        
        mu_end_window(ctx);
    }
}


/* ----   LOAD ROM INPUT ---- */
static  char logbuf[255];
static   int logbuf_updated = 0;

static void write_log(const char *text) {
  if (logbuf[0]) { strcat(logbuf, "\n"); }
  strcat(logbuf, text);
  logbuf_updated = 1;
}

static void load_rom_window(mu_Context *ctx){
    if (mu_begin_window(ctx, "Load ROM", mu_rect(10, 560, 300, 200))) {
    /* output ROM INFO panel */
    mu_layout_row(ctx, 1, (int[]) { -1 }, -25);
    mu_begin_panel(ctx, "ROM Info");
    mu_Container *panel = mu_get_current_container(ctx);
    mu_layout_row(ctx, 1, (int[]) { -1 }, -1);
    mu_text(ctx, logbuf);
    mu_end_panel(ctx);
    if (logbuf_updated) {
        panel->scroll.y = panel->content_size.y;
        logbuf_updated = 0;
    }

    /* input textbox + submit button */
    static char buf[128];
    int submitted = 0;
    mu_layout_row(ctx, 2, (int[]) { -70, -1 }, 0);
    if (mu_textbox(ctx, buf, sizeof(buf)) & MU_RES_SUBMIT) {
        mu_set_focus(ctx, ctx->last_id);
        submitted = 1;
    }
    if (mu_button(ctx, "Load")) { submitted = 1; }
    if (submitted) {
        if(InitializeGameROM(buf)){
            memset(device.memory, 0, 65536);
            InitializePowerOnState();
            InitializeBootROM();
            sprintf(buf, "GAME TITLE: %s\n", (char *)(&device.memory[0x0134]));
            write_log(buf); 
            const char* desc = cartridge_types[device.memory[0x0147]];
            if (desc) {
                sprintf(buf, "Cartridge type: %s\n", desc);
                write_log(buf);
            } else {
                sprintf(buf, "Cartridge type: Unknown (0x%02X)\n", device.memory[0x0147]);
                write_log(buf);
            }
        }
        else {
            sprintf(buf, "ERROR ROM doesn't exists at: %s\n", buf);
            write_log(buf);
        }
        buf[0] = '\0';
    }

    mu_end_window(ctx);
  }
}

static void gameboy_window(mu_Context *ctx){
    if (mu_begin_window(ctx, "Gameboy Window", mu_rect(350, 10, USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT+24))) {
        mu_Container *win = mu_get_current_container(ctx);
        mu_Rect r = mu_rect(win->body.x,win->body.y, win->body.w, win->body.h);
        mu_draw_image(ctx, r, framebuffer);
        mu_end_window(ctx);
    }
}

static void tiledata_window(mu_Context *ctx){
    if (mu_begin_window(ctx, "Tile data", mu_rect(350, 10, USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT+24))) {
        mu_Container *win = mu_get_current_container(ctx);
        mu_Rect r = mu_rect(win->body.x,win->body.y, win->body.w, win->body.h);
        mu_draw_image(ctx, r, tiledata);
        mu_end_window(ctx);
    }
}

void gui_process_frame(mu_Context *ctx) {
    mu_begin(ctx);
    debug_window(ctx);
    joypad_window(ctx);
    load_rom_window(ctx);
    gameboy_window(ctx);
    tiledata_window(ctx);
    mu_end(ctx);
}

#endif