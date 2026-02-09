/* Gameboy emulator by Leonardo Sinibaldi Started 19th July 2025. */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_mutex.h>

#include "hardware/device.h"

#include "../external/microui.h"
#include "gui/renderer.h"
#include "gui/gui.h"

#define FRAME_RATE_HZ 59.7
#define CYCLES_PER_FRAME (CLOCK_FREQ_HZ / FRAME_RATE_HZ)
#define MILLIS_PER_FRAME (1000L / FRAME_RATE_HZ)


extern DEVICE device;


/* This function will be transformed in a callback for the final user in order 
   to display data to the screen */
void process_frame_buffer(int x, int y, uint8_t color){
    if(y <= 1 && device.ppu.debug){
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

void process_input(SDL_Event *event){
    
    if(event->type == SDL_QUIT) exit(EXIT_SUCCESS);

    bool is_pressed = (event->type == SDL_KEYDOWN);
    bool button_just_pressed = false;

    switch (event->key.keysym.sym) {
        case SDLK_b:    if(is_pressed && !device.joypad.start)  button_just_pressed = true; device.joypad.start  = is_pressed; break;
        case SDLK_v:    if(is_pressed && !device.joypad.select) button_just_pressed = true; device.joypad.select = is_pressed; break;
        case SDLK_m:    if(is_pressed && !device.joypad.b)      button_just_pressed = true; device.joypad.b      = is_pressed; break;
        case SDLK_k:    if(is_pressed && !device.joypad.a)      button_just_pressed = true; device.joypad.a      = is_pressed; break;
        case SDLK_s:    if(is_pressed && !device.joypad.down)   button_just_pressed = true; device.joypad.down   = is_pressed; break;
        case SDLK_w:    if(is_pressed && !device.joypad.up)     button_just_pressed = true; device.joypad.up     = is_pressed; break;
        case SDLK_a:    if(is_pressed && !device.joypad.left)   button_just_pressed = true; device.joypad.left   = is_pressed; break;
        case SDLK_d:    if(is_pressed && !device.joypad.right)  button_just_pressed = true; device.joypad.right  = is_pressed; break;

        // Section for enabling or disabling layers rendered by ppu 
        case SDLK_1:    if(is_pressed) device.ppu.bg_enabled = !device.ppu.bg_enabled; break;
        case SDLK_2:    if(is_pressed) device.ppu.wn_enabled = !device.ppu.wn_enabled; break;
        case SDLK_3:    if(is_pressed) device.ppu.ob_enabled = !device.ppu.ob_enabled; break;
        case SDLK_p:    if(is_pressed) device.ppu.debug = !device.ppu.debug; break;
    }

    if(button_just_pressed){ // Request joypad interrupt
        device.memory[IF_REG] |= 0x10;
    }
}

void create_tile_data_grid(){
    for(int x=0; x<USER_WINDOW_WIDTH; x++){
        for(int y=0; y<USER_WINDOW_HEIGHT; y++){
            if(x % (8*SCALE_FACTOR) == 0 || y % (8*SCALE_FACTOR) == 0){
                tiledata[y][x] = 0xFF0000FF;
            }
        }
    }
}

void inspect_tile(int base_x, int base_y){
    uint8_t byte1, byte2;
    uint8_t bit_index;
    uint8_t color_bit1, color_bit0;
    uint8_t bg_color_number;
    uint8_t BGP;
    uint8_t color;

    int offset = (base_x * 18 + base_y) * 8 * 2;

    for (int y=0; y<8; y++){
        byte1 = device.memory[0x8000 + offset + y*2];
        byte2 = device.memory[0x8001 + offset + y*2];

        for(int x=0; x<8; x++){
            bit_index = 7 - (x % 8);

            color_bit1 = (byte2 >> bit_index) & 1;
            color_bit0 = (byte1 >> bit_index) & 1;

            bg_color_number = (color_bit1 << 1) | color_bit0;

            BGP = ReadMem(0xFF47); 
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

void inspect_tile_data(){
    
    for(int y = 0; y<18; y++){
        for(int x = 0; x<20; x++){
            inspect_tile(x, y);
        }
    }
    create_tile_data_grid();
}




#ifdef DEBUGGER_MODE
FILE *logger;
void InitializeLogger(FILE **logger){
    *logger = fopen("gameboy.log", "w");
    if(*logger == NULL){
        exit(1);
    }
    printf("[INFO] Log file initialized correctly\n");
}

void EndLogger(FILE **logger){
    fclose(*logger);
}

void logEmulatorSatus(){
    if(logger == NULL){
        printf("logger is NULL\n");
        exit(1);
    }
    char buf[128];
    memset(buf, 0, 128);
    if(!device.cpu.halted){
        GetEmulatorStatusFile(buf, sizeof(buf));
        fprintf(logger, "%*s", (int)sizeof(buf), buf);
    }
}
#endif

SDL_mutex *mutex;
SDL_Event event;

void EmulatorLoop(){
    uint64_t start_time = 0, end_time, sleep_duration_ms;

    while(device.cpu.running){
        SDL_LockMutex(mutex);

        start_time = SDL_GetTicks64();
        int cycles_this_frame = 0;

#ifndef DEBUGGER_MODE
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            process_input(&event);
        }
#endif


#ifdef DEBUGGER_MODE 
        size_t clock_limit = device.cpu.slowed ? device.cpu.slowed_at : CYCLES_PER_FRAME;
        while (cycles_this_frame < clock_limit && device.cpu.running){
#else
        while (cycles_this_frame < CYCLES_PER_FRAME && device.cpu.running){
#endif
                int cycles_executed = 0;
                // First, check if an interrupt needs to be serviced.
                cycles_executed += handleInterrupts();

#ifdef DEBUGGER_MODE
                if(!device.boot_rom_enabled) logEmulatorSatus();
#endif

                if (device.cpu.halted) {
                    cycles_executed += 4;
                } else {
                    uint8_t opcode = FetchByte(); 
                    //printf("instruction code executed: 0x%02X\n", opcode);
                    cycles_executed = instruction_table[opcode](&(device.cpu));
                }

                cycles_this_frame += cycles_executed;

                ppu_step(cycles_executed);
                timer_step(cycles_executed);
                dma_step(cycles_executed);
                serial_step(cycles_executed);

                // DEBUG INFO Written to serial data output by tests printend on console
                if(device.memory[0xFF01] >= 0 && device.memory[0xFF01] <= 127 && device.memory[0xFF02] == 0x81){
                    printf("%c",device.memory[0xFF01]);
                    device.memory[0xFF02] = 0;
                }
            }
            SDL_UnlockMutex(mutex);

#ifndef DEBUGGER_MODE
            r_clear(mu_color(0, 0, 0, 255));
            mu_Rect r = mu_rect(0,0,USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT);
            r_draw_image(r, USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT, (const uint32_t *)framebuffer);
            r_present();
#endif

            end_time = SDL_GetTicks64();

#ifdef DEBUGGER_MODE
            if(device.cpu.slowed){
                sleep_duration_ms = (1000/(double)(device.cpu.slowed_at)) - (end_time - start_time);
            }
            else{
                sleep_duration_ms = MILLIS_PER_FRAME - (end_time - start_time);
            }
#else
            sleep_duration_ms = MILLIS_PER_FRAME - (end_time - start_time);
#endif
            if (sleep_duration_ms > 0) {
                SDL_Delay(sleep_duration_ms);
            }

            inspect_tile_data();

        }
        }


int main(int argc, char **argv){
    if(argc <= 1){
        fprintf(stderr, "[ERROR] Usage: ./gameboy <path-to-ROM>\n");
        exit(1);
    }

    device.ppu.process_frame_buffer = process_frame_buffer;
    InitializeInstructionTable();
    InitializePowerOnState();
    InitializeBootROM();
    if(!InitializeCartridge(argv[1])){
        printf("[ERROR] Initialize cartridge failed\n");
        return 1;
    }
    PrintCartridgeInfo();

    mutex = SDL_CreateMutex();
    
    #ifdef DEBUGGER_MODE
        InitializeLogger(&logger);
        SDL_Thread *thread = SDL_CreateThread((SDL_ThreadFunction)EmulatorLoop, "EmulationThread", NULL);
        r_init("Gameboy Debugger", USER_WINDOW_WIDTH*3, USER_WINDOW_HEIGHT+200,  "fonts/DejaVuSans.ttf");
        mu_init(&ctx);
        ctx.text_width = gui_text_width;
        ctx.text_height = gui_text_height;

        while(1){
            while (SDL_PollEvent(&event)) {
                SDL_LockMutex(mutex);
                process_input(&event);
                SDL_UnlockMutex(mutex);

                switch (event.type) {
                    case SDL_QUIT: exit(EXIT_SUCCESS);
                    case SDL_MOUSEMOTION: mu_input_mousemove(&ctx, event.motion.x, event.motion.y); break;
                    case SDL_MOUSEWHEEL: mu_input_scroll(&ctx, 0, event.wheel.y * -30); break;
                    case SDL_TEXTINPUT: mu_input_text(&ctx, event.text.text); break;

                    case SDL_MOUSEBUTTONDOWN:
                    case SDL_MOUSEBUTTONUP: {
                    int b = button_map[event.button.button & 0xff];
                    if (b && event.type == SDL_MOUSEBUTTONDOWN) { mu_input_mousedown(&ctx, event.button.x, event.button.y, b); }
                    if (b && event.type ==   SDL_MOUSEBUTTONUP) { mu_input_mouseup(&ctx, event.button.x, event.button.y, b); }
                    break;
                    }

                    case SDL_KEYDOWN:
                    case SDL_KEYUP: {
                    int c = key_map[event.key.keysym.sym & 0xff];
                    if (c && event.type == SDL_KEYDOWN) { mu_input_keydown(&ctx, c); }
                    if (c && event.type ==   SDL_KEYUP) { mu_input_keyup(&ctx, c);   }
                    break;
                    }
                }
                
            }

            r_clear(mu_color(bg[0], bg[1], bg[2], 255));
            gui_process_frame(&ctx);

            mu_Command *cmd = NULL;
            while (mu_next_command(&ctx, &cmd)) {
                switch (cmd->type) {
                    case MU_COMMAND_TEXT: r_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color); break;
                    case MU_COMMAND_RECT: r_draw_rect(cmd->rect.rect, cmd->rect.color); break;
                    case MU_COMMAND_IMAGE: r_draw_image(cmd->image.rect, cmd->image.rect.w, cmd->image.rect.h, cmd->image.framebuffer);break;
                    case MU_COMMAND_ICON: r_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color); break;
                    case MU_COMMAND_CLIP: r_set_clip_rect(cmd->clip.rect); break;
                }
            }
            r_present();
        }
        SDL_WaitThread(thread, 0);
        EndLogger(&logger);
    #else
        r_init("Gameboy", USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT,  "fonts/DejaVuSans.ttf");
        EmulatorLoop();
    #endif
    r_quit();

    return 0;
}
