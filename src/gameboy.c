/* Gameboy emulator by Leonardo Sinibaldi Started 19th July 2025. */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

#include "hardware/cpu.h"
#include "hardware/memory.h"
#include "hardware/ppu.h"
#include "hardware/timer.h"
#include "hardware/joypad.h"

#include "gui/microui.h"
#include "gui/renderer.h"

#define FRAME_RATE_HZ 59.7
#define CYCLES_PER_FRAME (CLOCK_FREQ_HZ / FRAME_RATE_HZ)
#define NANOSECONDS_PER_FRAME (1000000000L / FRAME_RATE_HZ)

uint32_t framebuffer[USER_WINDOW_HEIGHT][USER_WINDOW_WIDTH] = {0};


/* This function will be transformed in a callback for the final user in order 
   to display data to the screen */
void process_frame_buffer(int x, int y, uint8_t color){
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


/* This function allows the cpu to correctly handle interrupts */
int handleInterrupts(CPU *cpu){
    uint8_t IE = ReadMem(IE_REG);
    uint8_t IF = ReadMem(IF_REG);

    uint8_t requested = IE & IF;

    if(!cpu->IME){
        if(requested != 0) cpu->halted = false; // pending interrupt wakes up cpu
        return 0;
    }


    if (requested == 0) {
        return 0;
    }

    // An interrupt is happening, so the CPU is no longer halted
    cpu->halted = false;
    cpu->IME = false; // Disable further interrupts

    // Push PC to the stack
    cpu->SP -= 2;
    WriteMem(cpu->SP, (uint8_t)(cpu->PC & 0xFF));
    WriteMem(cpu->SP + 1, (uint8_t)(cpu->PC >> 8));

    // Check interrupts in order of priority
    if (requested & 0x01) { // V-Blank
        memory[IF_REG] &= ~0x01; // Clear the request flag
        cpu->PC = 0x0040;
    } else if (requested & 0x02) { // LCD STAT
        memory[IF_REG] &= ~0x02;
        cpu->PC = 0x0048;
    } else if (requested & 0x04) { // Timer
        memory[IF_REG] &= ~0x04;
        cpu->PC = 0x0050;
    } else if (requested & 0x08) { // Serial
        memory[IF_REG] &= ~0x08;
        cpu->PC = 0x0058;
    } else if (requested & 0x10) { // Joypad
        memory[IF_REG] &= ~0x10;
        cpu->PC = 0x0060;
    }

    return 20;
}


void InitializePowerOnState(CPU *cpu, PPU *ppu){
    cpu->PC = 0x0000;
    cpu->SP = 0x0000;
    cpu->AF = 0x0000;
    cpu->BC = 0x0000;
    cpu->DE = 0x0000;
    cpu->HL = 0x0000;
    
    cpu->halted = false;
    cpu->running = true;
    cpu->IME = false;

    // Initialize PPU state properly
    ppu->mode = MODE_2_OAM_SCAN;
    ppu->cycle_counter = 0;
    ppu->ly = 0;

    // Initialize I/O registers
    memory[0xFF00] = 0xCF; // Joypad input
    memory[TIMA_REG] = 0x00; memory[TMA_REG] = 0x00; memory[TAC_REG] = 0x00;
    memory[0xFF10] = 0x80; memory[0xFF11] = 0xBF; memory[0xFF12] = 0xF3;
    memory[0xFF14] = 0xBF; memory[0xFF16] = 0x3F; memory[0xFF17] = 0x00;
    memory[0xFF19] = 0xBF; memory[0xFF1A] = 0x7F; memory[0xFF1B] = 0xFF;
    memory[0xFF1C] = 0x9F; memory[0xFF1E] = 0xBF; memory[0xFF20] = 0xFF;
    memory[0xFF21] = 0x00; memory[0xFF22] = 0x00; memory[0xFF23] = 0xBF;
    memory[0xFF24] = 0x77; memory[0xFF25] = 0xF3; memory[0xFF26] = 0xF1;
    memory[0xFF41] = 0x02; // STAT - Start in mode 2 (OAM scan)
    memory[0xFF42] = 0x00; // SCY
    memory[0xFF43] = 0x00; // SCX
    memory[0xFF44] = 0x00; // LY - will be updated by PPU
    memory[0xFF45] = 0x00; // LYC
    memory[0xFF47] = 0xE4; // BGP - Better palette: 11 10 01 00
    memory[0xFF48] = 0xFF; memory[0xFF49] = 0xFF;
    memory[0xFF4A] = 0x00; memory[0xFF4B] = 0x00;
    memory[IE_REG] = 0x00;
}

// Add CPU state debugging
void print_cpu_state(CPU *cpu) {
    printf("PC:%04X SP:%04X AF:%04X BC:%04X DE:%04X HL:%04X\n Halted: %d, IME: %d, Running: %d, boot ROM enabled: %d \n\n", 
           cpu->PC, cpu->SP, cpu->AF, cpu->BC, cpu->DE, cpu->HL, cpu->halted, cpu->IME, cpu->running, boot_rom_enabled);
}

void create_dummy_header() {
    // This is the correct, official Nintendo logo A
    uint8_t nintendo_logo[48] = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
        0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
        0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
    };

    // Copy the logo data into the correct memory location
    for (int i = 0; i < 48; ++i) {
        memory[0x0104 + i] = nintendo_logo[i];
    }

    // A valid header checksum. The boot ROM also verifies this.
    memory[0x014D] = 0xEA;
}

void InitializeBootROM() {
    FILE *bootROM = fopen("gb-bootroms/bin/dmg.bin", "rb");
    if(bootROM){
        fread(boot, 256, 1, bootROM);
        fclose(bootROM);
    }
}

void InitializeGameROM(char* romPath) {
    FILE *program = fopen(romPath, "rb");
    size_t program_length;
    if(program){
        fseek(program, 0, SEEK_END);
        program_length = ftell(program);
        fseek(program, 0, SEEK_SET);
        fread(memory, program_length, 1, program);
        fclose(program);
    }
}

void process_input(SDL_Event *event){
    bool is_pressed = (event->type == SDL_KEYDOWN);
    bool button_just_pressed = false;

    switch (event->key.keysym.sym) {
        case SDLK_b:    if(is_pressed && !joypad.start)  button_just_pressed = true; joypad.start  = is_pressed; break;
        case SDLK_v:    if(is_pressed && !joypad.select) button_just_pressed = true; joypad.select = is_pressed; break;
        case SDLK_m:    if(is_pressed && !joypad.b)      button_just_pressed = true; joypad.b      = is_pressed; break;
        case SDLK_k:    if(is_pressed && !joypad.a)      button_just_pressed = true; joypad.a      = is_pressed; break;
        case SDLK_s:    if(is_pressed && !joypad.down)   button_just_pressed = true; joypad.down   = is_pressed; break;
        case SDLK_w:    if(is_pressed && !joypad.up)     button_just_pressed = true; joypad.up     = is_pressed; break;
        case SDLK_a:    if(is_pressed && !joypad.left)   button_just_pressed = true; joypad.left   = is_pressed; break;
        case SDLK_d:    if(is_pressed && !joypad.right)  button_just_pressed = true; joypad.right  = is_pressed; break;
    }

    if(button_just_pressed){ // Request joypad interrupt
        memory[IF_REG] |= 0x10;
    }
}

#ifdef DEBUG_TEST_LOG
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

void logEmulatorSatus(FILE **logger, CPU *cpu){
    if(*logger == NULL){
        printf("logger is NULL\n");
        exit(1);
    }
    uint8_t a =  cpu->AF >> 8;
    uint8_t f = (cpu->AF & 0xFF);
    uint8_t b =  cpu->BC >> 8;
    uint8_t c = (cpu->BC & 0xFF);
    uint8_t d =  cpu->DE >> 8;
    uint8_t e = (cpu->DE & 0xFF);
    uint8_t h =  cpu->HL >> 8;
    uint8_t l = (cpu->HL & 0xFF);
    fprintf(*logger, "A: %02X F: %02X B: %02X C: %02X D: %02X E: %02X H: %02X L: %02X SP: %04X PC: 00:%04X (%02X %02X %02X %02X)\n", a, f, b, c, d, e, h, l, cpu->SP, cpu->PC, ReadMem(cpu->PC), ReadMem(cpu->PC+1), ReadMem(cpu->PC+2), ReadMem(cpu->PC+3));
}
#endif

/* ---- MICROUI STUFF ---- */
int text_width(mu_Font font, const char *str, int len){
    return len;
}

int text_height(mu_Font font){
    return 12;
}


mu_Context ctx = {0};

static float bg[3] = { 90, 95, 100 };

int main(int argc, char **argv){
    if(argc <= 1){
        fprintf(stderr, "[ERROR] Usage: ./gameboy <path-to-ROM>\n");
        argv[1] = "tests/gb-test-roms/mem_timing/individual/01-read_timing.gb";
    }


    CPU cpu = {0};
    PPU ppu = {0};
    ppu.process_frame_buffer = process_frame_buffer;
    InitializeInstructionTable();
    InitializePowerOnState(&cpu, &ppu);
    InitializeBootROM();
    InitializeGameROM(argv[1]);

    #ifdef DEBUG_TEST_LOG
        FILE *logger = NULL;
        InitializeLogger(&logger);
    #endif
    //create_dummy_header();
    //boot_rom_enabled = false;

    struct timespec start_time, end_time;
    long sleep_duration_ns;

    /* --- SDL init ---*/
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("Gameboy", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, USER_WINDOW_WIDTH, USER_WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,        
        SDL_TEXTUREACCESS_STREAMING,
        USER_WINDOW_WIDTH,
        USER_WINDOW_HEIGHT
    );
    SDL_Event event;

    mu_init(&ctx);
    ctx.text_width = text_width;
    ctx.text_height = text_height;

    while(cpu.running){
        mu_begin(&ctx);

        clock_gettime(CLOCK_MONOTONIC, &start_time);

        int cycles_this_frame = 0;
        while (cycles_this_frame < CYCLES_PER_FRAME && cpu.running){
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    cpu.running = 0;
                }
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q) {
                    printf("=== DEBUG INFO ===\n");
                    print_cpu_state(&cpu);
                    printf("PPU: LY=%d, Mode=%d, Cycles=%zu\n", ppu.ly, ppu.mode, ppu.cycle_counter);
                    printf("LCDC=0x%02X, STAT=0x%02X, BGP=0x%02X\n", 
                        memory[0xFF40], memory[0xFF41], memory[0xFF47]);
                }
                process_input(&event);
            }

            

            int cycles_executed = 0;

            // First, check if an interrupt needs to be serviced.
            cycles_executed += handleInterrupts(&cpu);
            
            #ifdef DEBUG_TEST_LOG
                    if(!boot_rom_enabled) logEmulatorSatus(&logger, &cpu);
            #endif

            if (cpu.halted) {
                cycles_executed += 4;
            } else {
                uint8_t opcode = FetchByte(&cpu); 
                cycles_executed = instruction_table[opcode](&cpu);
            }

            cycles_this_frame += cycles_executed;

            ppu_step(&ppu, cycles_executed);
            timer_step(cycles_executed);
            dma_step(cycles_executed);

            // DEBUG INFO Written to serial data output by tests printend on console
            if(memory[0xFF01] >= 0 && memory[0xFF01] <= 127 && memory[0xFF02] == 0x81){
                printf("%c",memory[0xFF01]);
                memory[0xFF02] = 0;
            }
        }
        
        SDL_UpdateTexture(texture, NULL, framebuffer, USER_WINDOW_WIDTH * sizeof(uint32_t));  // Update the texture with the new pixel data
        SDL_RenderClear(renderer); // Clear the renderer
        SDL_RenderCopy(renderer, texture, NULL, NULL); // Copy the texture to the renderer
        SDL_RenderPresent(renderer); // Present the renderer

        /*r_clear(mu_color(bg[0], bg[1], bg[2], 255));
        mu_Command *cmd = NULL;
        while (mu_next_command(&ctx, &cmd)) {
        switch (cmd->type) {
            case MU_COMMAND_TEXT: r_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color); break;
            case MU_COMMAND_RECT: r_draw_rect(cmd->rect.rect, cmd->rect.color); break;
            case MU_COMMAND_ICON: r_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color); break;
            case MU_COMMAND_CLIP: r_set_clip_rect(cmd->clip.rect); break;
        }
        }
        r_present();*/

        clock_gettime(CLOCK_MONOTONIC, &end_time);
        long time_elapsed_ns = (end_time.tv_sec - start_time.tv_sec) * 1000000000L +
                               (end_time.tv_nsec - start_time.tv_nsec);

        sleep_duration_ns = NANOSECONDS_PER_FRAME - time_elapsed_ns;

        if (sleep_duration_ns > 0) {
            struct timespec sleep_spec = {0, sleep_duration_ns};
            nanosleep(&sleep_spec, NULL);
        }
    }

    // --- Cleanup ---
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    #ifdef DEBUG_TEST_LOG
        EndLogger(&logger);
    #endif

    return 0;
}