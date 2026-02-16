#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../../external/microui.h"
#include "renderer.h"
#include "gui.h"

uint32_t framebuffer[USER_WINDOW_HEIGHT][USER_WINDOW_WIDTH] = {0};
uint32_t tiledata[USER_WINDOW_HEIGHT][USER_WINDOW_WIDTH] = {0};

static DEVICE *device = NULL;
static GuiExternalInterface *gui_ext = NULL;

/* ---- MICROUI STUFF ---- */
/* Allocation of ctx is done on heap
 * because in this way it can survive a reload
 * of the libgui without copying the entire struct
 */
typedef struct {
    mu_Context *ctx;
    void  *r_ctx;
    SDL_mutex  *emu_gui_mutex;
    DEVICE     *device;
    GuiExternalInterface *gui_ext;
} gui_state_t;

static mu_Context *ctx = NULL;
static SDL_mutex *gui_mutex = NULL;
static float bg[3] = { 90, 95, 100 };
static char buffer[512] = {0};

static int gui_text_width(mu_Font font, const char *text, int len) {
    if (len == -1) { len = strlen(text); }
    return r_get_text_width(text, len);
}

static int gui_text_height(mu_Font font) {
    return r_get_text_height();
}



/* This function will be transformed in a callback for the final user in order 
   to display data to the screen */
void gui_process_framebuffer(int x, int y, uint8_t color){
    if(y <= 1 && device->ppu.debug){
        printf("Frame buffer at x = %d y = %d color = %d\n", x, y, color);
    }
    uint32_t final_color;
    switch(color){
        case 0x00: final_color = 0xFFFFFFFF; break;
        case 0x01: final_color = 0xC0C0C0C0; break;
        case 0x02: final_color = 0x2C2C2C2C; break;
        case 0x03: final_color = 0x00000000; break;
    }

    for(int i = 0; i<SCALE_FACTOR; i++){
        for(int j=0; j<SCALE_FACTOR; j++){
            framebuffer[SCALE_FACTOR*y+i][SCALE_FACTOR*x+j] = final_color;
        }
    }
};

#ifdef DEBUGGER_MODE

static void create_tile_data_grid(){
    for(int x=0; x<USER_WINDOW_WIDTH; x++){
        for(int y=0; y<USER_WINDOW_HEIGHT; y++){
            if(x % (8*SCALE_FACTOR) == 0 || y % (8*SCALE_FACTOR) == 0){
                tiledata[y][x] = 0xFF0000FF;
            }
        }
    }
}

static void inspect_tile(int base_x, int base_y){
    uint8_t byte1, byte2;
    uint8_t bit_index;
    uint8_t color_bit1, color_bit0;
    uint8_t bg_color_number;
    uint8_t BGP;
    uint8_t color;

    int offset = (base_x * 18 + base_y) * 8 * 2;

    for (int y=0; y<8; y++){
        byte1 = device->memory[0x8000 + offset + y*2];
        byte2 = device->memory[0x8001 + offset + y*2];

        for(int x=0; x<8; x++){
            bit_index = 7 - (x % 8);

            color_bit1 = (byte2 >> bit_index) & 1;
            color_bit0 = (byte1 >> bit_index) & 1;

            bg_color_number = (color_bit1 << 1) | color_bit0;

            BGP = gui_ext->read_mem(0xFF47); 
            color = (BGP >> (bg_color_number * 2)) & 0x03;
            

            uint32_t final_color;
            switch(color){
                case 0x00: final_color = 0xFFFFFFFF; break;
                case 0x01: final_color = 0xC0C0C0C0; break;
                case 0x02: final_color = 0x2C2C2C2C; break;
                case 0x03: final_color = 0x00000000; break;
            }

            for(int i = 0; i<SCALE_FACTOR; i++){
                for(int j=0; j<SCALE_FACTOR; j++){
                    tiledata[SCALE_FACTOR*(y+base_y*8)+i][SCALE_FACTOR*(x+base_x*8)+j] = final_color;
                }
            }

        }
    }
}

void gui_process_tiledata(){
    
    for(int y = 0; y<18; y++){
        for(int x = 0; x<20; x++){
            inspect_tile(x, y);
        }
    }
    create_tile_data_grid();
}


void *gui_pre_reload() {
    SDL_LockMutex(gui_mutex);
    device->ppu.process_frame_buffer = NULL;
    SDL_UnlockMutex(gui_mutex);

    // Allocated on heap to return a valid pointer that can survive dlopen
    gui_state_t *state = malloc(sizeof(*state));

    if(!state) {
        fprintf(stderr, "[GUI] Error buy more memory!\n");
        exit(-1);
    }
    
    state->ctx = ctx;
    state->r_ctx = r_pre_reload();
    state->device = device;
    state->emu_gui_mutex = gui_mutex;
    state->gui_ext = gui_ext;
    return state;
}

void gui_post_reload(void *state) {
    if(state == NULL) {
        // TODO : Add checks for each field
        fprintf(stderr, "[GUI] Error after reload, invalid state!\n");
        exit(-1);
    }

    gui_state_t *g_ctx = (gui_state_t *)state;
    ctx = g_ctx->ctx;
    mu_post_reload(ctx);
    ctx->text_width = gui_text_width;
    ctx->text_height = gui_text_height;

    device = g_ctx->device;
    gui_ext = g_ctx->gui_ext;    
    gui_mutex = g_ctx->emu_gui_mutex;

    SDL_LockMutex(gui_mutex);
    device->ppu.process_frame_buffer = gui_process_framebuffer;
    SDL_UnlockMutex(gui_mutex);

    r_post_reload(g_ctx->r_ctx);
}

static void slow_clock_setting(mu_Context *ctx) {
    static char slow_freq_buf[255];
    bool submitted = false;
    mu_layout_row(ctx, 2, (int[]) { -70, -1 }, 0);
    if (mu_textbox(ctx, slow_freq_buf, sizeof(slow_freq_buf)) & MU_RES_SUBMIT) {
        mu_set_focus(ctx, ctx->last_id);
        submitted = true;
    }

    const char* slow_text = device->cpu.slowed ? "Std clock" : "Slow clock";
    if(mu_button(ctx, slow_text)){
        submitted = true;
    }
    if(submitted) {
        uint64_t freq = strtoll(slow_freq_buf, NULL, 10);
        if(freq != 0) device->cpu.slowed_at = freq;
        device->cpu.slowed = ! device->cpu.slowed;
    }
}

static void debug_window(mu_Context *ctx) {
    //printf("gui_ext->get_emulator_status is at %p\n", gui_ext->get_emulator_status);
    if (mu_begin_window(ctx, "Debug info", mu_rect(10, 10, 300, 450))) {
        //GetEmulatorStatus(buffer, sizeof(buffer));
        gui_ext->get_emulator_status(buffer, sizeof(buffer));
        mu_Container *win = mu_get_current_container(ctx);
        win->rect.w = mu_max(win->rect.w, 320);
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
            memset(device->memory, 0, 65536);
            gui_ext->restart_emulator();
            gui_ext->init_boot_rom();
            free(device->cartridge.data); // TODO create a shutdown function for cartridge
            gui_ext->init_cartridge(device->cartridge.gamePath);
            gui_ext->print_cartridge_info();
        }

        mu_layout_end_column(ctx);
        mu_layout_begin_column(ctx);


        const char* halt_text = device->cpu.halted ? "De-Halt CPU" : "Halt CPU";
        if(mu_button(ctx, halt_text)){
            device->cpu.halted = !device->cpu.halted;
        }
        
        slow_clock_setting(ctx);
        mu_layout_end_column(ctx);  
        mu_end_window(ctx);
    }
}

static void joypad_window(mu_Context *ctx){
    if (mu_begin_window(ctx, "Joypad", mu_rect(10, 500, 300, 50))) {
        mu_Container *win = mu_get_current_container(ctx);
        win->rect.w = mu_max(win->rect.w, 100);
        win->rect.h = mu_max(win->rect.h, 200);
        mu_layout_row(ctx, 2, (int[]) { 0, -1 }, -100);
        mu_layout_begin_column(ctx);
        if(mu_button(ctx, "Start")){
            device->joypad.start = true;
            device->memory[IF_REG] |= 0x10;
        }

        if(mu_button(ctx, "Select")){
            device->joypad.select = true;
            device->memory[IF_REG] |= 0x10;
        }

        if(mu_button(ctx, "Reset pressed")){
            device->joypad.select = false;
            device->joypad.start = false;
        }
        mu_layout_end_column(ctx);
        mu_layout_begin_column(ctx);
        if(mu_button(ctx, "Start")){
            device->joypad.start = true;
            device->memory[IF_REG] |= 0x10;
        }

        if(mu_button(ctx, "Select")){
            device->joypad.select = true;
            device->memory[IF_REG] |= 0x10;
        }

        if(mu_button(ctx, "Reset pressed")){
            device->joypad.select = false;
            device->joypad.start = false;
        }
        mu_layout_end_column(ctx);

        mu_end_window(ctx);
    }
}


/* ----   LOAD ROM INPUT ---- */
static  char logbuf[1000];
static   int logbuf_updated = 0;

static void write_log(const char *text) {
  if (logbuf[0]) { strcat(logbuf, "\n"); }
  strcat(logbuf, text);
  logbuf_updated = 1;
}

static void load_rom_window(mu_Context *ctx){
    if (mu_begin_window(ctx, "Load ROM", mu_rect(10, 1000, 300, 200))) {
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
    static char buf[255];
    int submitted = 0;
    mu_layout_row(ctx, 2, (int[]) { -70, -1 }, 0);
    if (mu_textbox(ctx, buf, sizeof(buf)) & MU_RES_SUBMIT) {
        mu_set_focus(ctx, ctx->last_id);
        submitted = 1;
    }
    if (mu_button(ctx, "Load")) { submitted = 1; }
    if (submitted) {
        free(device->cartridge.data); // TODO shutdown function for cartridge
        if(gui_ext->init_cartridge(buf)){
            memset(device->memory, 0, 65536);
            gui_ext->restart_emulator();
            gui_ext->init_boot_rom();
            write_log(device->cartridge.title); 
            /*const char* desc = cartridge_types[device->memory[0x0147]];
            if (desc) {
                sprintf(buf, "Cartridge type: %s\n", desc);
                write_log(buf);
            } else {
                sprintf(buf, "Cartridge type: Unknown (0x%02X)\n", device->memory[0x0147]);
                write_log(buf);
            }*/
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
    if (mu_begin_window(ctx, "Gameboy Window", mu_rect(340, 10, USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT+24))) {
        mu_Container *win = mu_get_current_container(ctx);
        mu_Rect r = mu_rect(win->body.x,win->body.y, win->body.w, win->body.h);
        mu_draw_image(ctx, r, framebuffer);
        mu_end_window(ctx);
    }
}

static void tiledata_window(mu_Context *ctx){
    if (mu_begin_window(ctx, "Tile data", mu_rect(340 + 20 + USER_WINDOW_WIDTH, 10, USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT+24))) {
        mu_Container *win = mu_get_current_container(ctx);
        mu_Rect r = mu_rect(win->body.x,win->body.y, win->body.w, win->body.h);
        mu_draw_image(ctx, r, tiledata);
        mu_end_window(ctx);
    }
}


static void gui_debugger_frame(mu_Context *ctx) {
    mu_begin(ctx);
    debug_window(ctx);
    joypad_window(ctx);
    load_rom_window(ctx);
    gameboy_window(ctx);
    tiledata_window(ctx);
    mu_end(ctx);
}
#endif //DEBUGGER_MODE

void gui_init(const char* window_title, 
        int window_width, 
        int window_height, 
        const char *font_path, 
        DEVICE *dev,
        GuiExternalInterface *gui_external_interface,
        SDL_mutex *emu_gui_mutex)
{
    ctx = malloc(sizeof(*ctx));
    if(!ctx) {
        fprintf(stderr, "[GUI] ERROR: buy more memory!\n");
        exit(-1);
    }
    device = dev;
    gui_ext = gui_external_interface;
    gui_mutex = emu_gui_mutex;

    SDL_LockMutex(gui_mutex);
    device->ppu.process_frame_buffer = gui_process_framebuffer;
    SDL_UnlockMutex(gui_mutex);

    r_init(window_title, window_width, window_height, font_path);
    mu_init(ctx);
    ctx->text_width = gui_text_width;
    ctx->text_height = gui_text_height;
}

void gui_process_event(SDL_Event *event) {
    if(!device || !ctx || !gui_ext) {
        fprintf(stderr, "[GUI] ERROR: Call gui_init first!\n");
        exit(-1);
    }
    switch (event->type) {
    case SDL_QUIT: exit(EXIT_SUCCESS);
    case SDL_MOUSEMOTION: mu_input_mousemove(ctx, event->motion.x, event->motion.y); break;
    case SDL_MOUSEWHEEL: mu_input_scroll(ctx, 0, event->wheel.y * -30); break;
    case SDL_TEXTINPUT: mu_input_text(ctx, event->text.text); break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        int b = button_map[event->button.button & 0xff];
        if (b && event->type == SDL_MOUSEBUTTONDOWN) { mu_input_mousedown(ctx, event->button.x, event->button.y, b); }
        if (b && event->type ==   SDL_MOUSEBUTTONUP) { mu_input_mouseup(ctx, event->button.x, event->button.y, b); }
        break;
    }

    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        int c = key_map[event->key.keysym.sym & 0xff];
        if (c && event->type == SDL_KEYDOWN) { mu_input_keydown(ctx, c); }
        if (c && event->type ==   SDL_KEYUP) { mu_input_keyup(ctx, c);   }
        break;
    }
    }
}

void gui_render() {
#ifdef DEBUGGER_MODE
    r_clear(mu_color(bg[0], bg[1], bg[2], 255));
    gui_process_tiledata();
    gui_debugger_frame(ctx);

    mu_Command *cmd = NULL;
    while (mu_next_command(ctx, &cmd)) {
        switch (cmd->type) {
            case MU_COMMAND_TEXT: r_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color); break;
            case MU_COMMAND_RECT: r_draw_rect(cmd->rect.rect, cmd->rect.color); break;
            case MU_COMMAND_IMAGE: r_draw_image(cmd->image.rect, cmd->image.rect.w, cmd->image.rect.h, cmd->image.framebuffer);break;
            case MU_COMMAND_ICON: r_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color); break;
            case MU_COMMAND_CLIP: r_set_clip_rect(cmd->clip.rect); break;
        }
    }
#else
    r_clear(mu_color(0, 0, 0, 255));
    mu_Rect r = mu_rect(0,0,USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT);
    r_draw_image(r, USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT, (const uint32_t *)framebuffer);
#endif
    r_present();
}

void gui_quit() {
    r_quit();
}
