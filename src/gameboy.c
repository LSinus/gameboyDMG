/* Copyright (c) Leonardo Sinibaldi 2025-2026
 * Gameboy emulator sarted 19th July 2025.   
 */

#include "gui/renderer.h"

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
#include "gui/gui_hotreload.h"
#include <dlfcn.h>

#ifdef DEBUGGER_MODE 
#include <dlfcn.h>
#endif

#define FRAME_RATE_HZ 59.7
#define CYCLES_PER_FRAME (CLOCK_FREQ_HZ / FRAME_RATE_HZ)
#define MILLIS_PER_FRAME (1000L / FRAME_RATE_HZ)


extern DEVICE device;



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

SDL_mutex *gui_mutex;
SDL_Event event;

void EmulatorLoop(){
    uint64_t start_time = 0, end_time, sleep_duration_ms;

    while(device.cpu.running){
        SDL_LockMutex(gui_mutex);

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
            SDL_UnlockMutex(gui_mutex);

#ifndef DEBUGGER_MODE
            /* It is safe to call directly this function because 
             * for non debugger builds the gui is only statically linked
             * */
            gui_render();
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
        }
        }

static GuiExternalInterface gui_ext = {0};

int main(int argc, char **argv){
    if(argc <= 1){
        fprintf(stderr, "Error: Usage: %s <path-to-ROM>\n", argv[0]);
        exit(1);
    }
/*
    gui_ext.read_mem            = (uint8_t (*)(uint16_t))dlsym(RTLD_DEFAULT, "ReadMem");
    gui_ext.write_mem           = (void (*)(uint16_t, uint8_t))dlsym(RTLD_DEFAULT, "WriteMem");
    gui_ext.get_emulator_status = (void (*)(char*, size_t))dlsym(RTLD_DEFAULT, "GetEmulatorStatus");
    gui_ext.restart_emulator    = (void (*)(void))dlsym(RTLD_DEFAULT, "InitializePowerOnState");
    gui_ext.init_boot_rom       = (void (*)(void))dlsym(RTLD_DEFAULT, "InitializeBootROM");
    gui_ext.init_cartridge      = (bool (*)(char*))dlsym(RTLD_DEFAULT, "InitializeCartridge");
    gui_ext.print_cartridge_info = (void (*)(void))dlsym(RTLD_DEFAULT, "PrintCartridgeInfo");
*/
    gui_ext.read_mem            = ReadMem;
    gui_ext.write_mem           = WriteMem;
    gui_ext.get_emulator_status = GetEmulatorStatus;
    gui_ext.restart_emulator    = InitializePowerOnState; 
    gui_ext.init_boot_rom       = InitializeBootROM; 
    gui_ext.init_cartridge      = InitializeCartridge;
    gui_ext.print_cartridge_info = PrintCartridgeInfo; 


    InitializeInstructionTable();
    InitializePowerOnState();
    InitializeBootROM();
    if(!InitializeCartridge(argv[1])){
        printf("[ERROR] Initialize cartridge failed\n");
        return 1;
    }
    PrintCartridgeInfo();

    gui_mutex = SDL_CreateMutex();
#ifdef DEBUGGER_MODE
    if (!reload_libgui()) exit(-1); 

    gui_init("Gameboy Debugger", USER_WINDOW_WIDTH*3, USER_WINDOW_HEIGHT+400, 
            "fonts/DejaVuSans.ttf", 
            &device, 
            &gui_ext,
            gui_mutex);
    InitializeLogger(&logger);
    SDL_Thread *thread = SDL_CreateThread((SDL_ThreadFunction)EmulatorLoop, "EmulationThread", NULL);
    
    while(true){
        while (SDL_PollEvent(&event)) {
            SDL_LockMutex(gui_mutex);
            process_input(&event);
            if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_h) {
                void *state = gui_pre_reload();
                if(!reload_libgui()) exit(-1);
                gui_post_reload(state, &device, &gui_ext);
                printf("gui_post_reload succesfully executed\n");
            }
            gui_process_event(&event);
            SDL_UnlockMutex(gui_mutex);
        }
        gui_render();
    }
    SDL_WaitThread(thread, 0);
    EndLogger(&logger);
#else
    gui_init("Gameboy", USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT, "fonts/DejaVuSans.ttf", &device, &gui_ext, gui_mutex);
    EmulatorLoop();
#endif
    gui_quit();

    return 0;
}
