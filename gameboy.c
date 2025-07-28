/* Gameboy emulator by Leonardo Sinibaldi
   Started 19th July 2025.
*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>

#include <SDL2/SDL.h>

#define CLOCK_FREQ_HZ 4194304
#define DIV_INC_FREQ_HZ 16384
#define FRAME_RATE_HZ 59.7
#define CYCLES_PER_FRAME (CLOCK_FREQ_HZ / FRAME_RATE_HZ)
#define NANOSECONDS_PER_FRAME (1000000000L / FRAME_RATE_HZ)

#define WINDOW_WIDTH 160 
#define WINDOW_HEIGHT 144

#define SCALE_FACTOR 4

#define USER_WINDOW_WIDTH WINDOW_WIDTH * SCALE_FACTOR
#define USER_WINDOW_HEIGHT WINDOW_HEIGHT * SCALE_FACTOR

uint32_t framebuffer[USER_WINDOW_HEIGHT][USER_WINDOW_WIDTH] = {0};

bool boot_rom_enabled = true;
uint8_t boot[256];
uint8_t memory[65536];

/* Definition of CPU for Nintendo Gameboy */
typedef struct CPU {
    uint16_t AF; // accumulator and flags
    uint16_t BC;
    uint16_t DE;
    uint16_t HL;
    uint16_t SP; // stack pointer
    uint16_t PC; // program counter


    bool running;
    bool halted;
    bool IME;
} CPU;

typedef enum {
    MODE_0_HBLANK,
    MODE_1_VBLANK,
    MODE_2_OAM_SCAN,
    MODE_3_DRAWING
} PPU_MODE;

/* Definition of PPU state machine */
typedef struct PPU {
    PPU_MODE mode;
    size_t cycle_counter;
    uint8_t ly;
} PPU;

/* Definition of Timer state machine */
typedef struct TIMER {
    size_t div_cycle_counter;
    size_t timer_cycle_counter;
} TIMER;

/* JoyPad state struct */
typedef struct JOYPAD {
    bool start, select, b, a;
    bool down, up, left, right;
} JOYPAD;

JOYPAD joypad = {0};

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

/* This function returns the current PPU mode */
uint8_t ppu_get_mode() {
    return memory[0xFF41] & 0x03;
}

uint8_t ReadMem(uint16_t addr){
    uint8_t ppu_mode = ppu_get_mode();

    // Check for VRAM read restrictions
    //Check for VRAM read restrictions
    uint8_t LCDC = memory[0xFF40];
    if((LCDC >> 7) == 1){ // LCD and PPU are enabled
        if (addr >= 0x8000 && addr <= 0x9FFF) {
            if (ppu_mode == MODE_3_DRAWING) {
                return 0xFF; // VRAM is inaccessible, return 0xFF
            }
        }

        // Check for OAM read restrictions
        if (addr >= 0xFE00 && addr <= 0xFE9F) {
            if (ppu_mode == MODE_2_OAM_SCAN || ppu_mode == MODE_3_DRAWING) {
                return 0xFF; // OAM is inaccessible, return 0xFF
            }
        }
    }

    if(boot_rom_enabled && addr < 0x0100){
        return boot[addr];
    }

    if(addr == 0xFF00){
        uint8_t P1 = memory[0xFF00];
        P1 |= 0x0F; // all buttons unpressed (0 pressed 1 unpressed)
        if((P1 & 0x10) == 0){ // D-Pad buttons
            if (joypad.right) P1 &= ~0x01; // Bit 0 (Right)
            if (joypad.left)  P1 &= ~0x02; // Bit 1 (Left)
            if (joypad.up)    P1 &= ~0x04; // Bit 2 (Up)
            if (joypad.down)  P1 &= ~0x08; // Bit 3 (Down)
        }
        if ((P1 & 0x20) == 0) { // Action buttons
            if (joypad.a)      P1 &= ~0x01; // Bit 0 (A)
            if (joypad.b)      P1 &= ~0x02; // Bit 1 (B)
            if (joypad.select) P1 &= ~0x04; // Bit 2 (Select)
            if (joypad.start)  P1 &= ~0x08; // Bit 3 (Start)
        }
        return P1;
    }

    return memory[addr];
}


/* This function fetches and returns a byte from memory at the address of
   the program counter and increments it. */
uint8_t FetchByte(CPU *cpu){
    return ReadMem(cpu->PC++);;
}

/* This function fetches and return a 16-bit word from memory at the address of
   the program counter and increments it. The word is stored in little endian
   so the shift is needed to return the right value */
uint16_t FetchWord(CPU *cpu){
    uint16_t lsb = (uint16_t)FetchByte(cpu);
    uint16_t msb = (uint16_t)FetchByte(cpu);
    return (msb << 8) | lsb;
}

/* This function sets a mode for PPU reflecting the state inside the IO
   register visible at 0xFF41 on the memory bud
*/
void ppu_set_mode(PPU *ppu, PPU_MODE mode){
    ppu->mode = mode;
    memory[0xFF41] = (memory[0xFF41] & 0b11111100) | mode;
}


void WriteMem(uint16_t addr, uint8_t data){
    uint8_t ppu_mode = ppu_get_mode();

    //Check for VRAM read restrictions
    uint8_t LCDC = memory[0xFF40];
    if((LCDC >> 7) == 1){ // LCD and PPU are enabled
        if (addr >= 0x8000 && addr <= 0x9FFF) {
            if (ppu_mode == MODE_3_DRAWING) {
                return; // VRAM is inaccessible
            }
        }

        // Check for OAM read restrictions
        if (addr >= 0xFE00 && addr <= 0xFE9F) {
            if (ppu_mode == MODE_2_OAM_SCAN || ppu_mode == MODE_3_DRAWING) {
                return; // OAM is inaccessible
            }
        }
    }

    if (addr == 0xFF50) {
        boot_rom_enabled = false; // Disable the boot ROM
    }

    if(addr != 0xFF04) memory[addr] = data;
    else memory[addr] = 0x00; // writing DIV register resets it
}



/* This function gets data from VRAM and sends it to LCD framebuffer at the end 
   of the execution of this function a new line is visible on the screen. */
void ppu_scanline(PPU *ppu){
    // cycle for an entire line
    for(uint8_t x = 0; x < WINDOW_WIDTH; x++){
        /* It is important to look at SCY and SCX to map to world cordinates 
           in order to apply background scrolling. */
        uint8_t SCY = ReadMem(0xFF42);
        uint8_t SCX = ReadMem(0xFF43);

        uint8_t LCDC = ReadMem(0xFF40);

        uint8_t world_x = SCX + x;
        uint8_t world_y = SCY + ppu->ly; 

        // Tiles are 8x8 pixels so the index is the coordinates diveded by 8
        uint8_t tile_x  = world_x / 8;
        uint8_t tile_y  = world_y / 8;

        /* Tile id from VRAM Tile-Map. It is important to access memory array 
           directly here without using ReadMeme because in this state the memory
           blocks reads in VRAM and OAM for CPU */
        uint16_t tile_map_addr = ((LCDC & 0b00001000) == 0 ? 0x9800 : 0x9C00); // Third bit of LCDC indicates the tile map location
        uint16_t tile_id_addr  = tile_map_addr + (tile_y * 32) + tile_x; // The address of the tile that is needed
        uint8_t  tile_id       = memory[tile_id_addr]; // The tile id picked directly from memory  

        uint16_t tile_data_addr;

        if((LCDC & 0x10) != 0){ // Use 0x8000 method (unsigned)
            tile_data_addr = 0x8000;
            tile_data_addr += tile_id * 16;
        }
        else{ // Use 0x8800 method (signed)
            tile_data_addr = 0x9000;
            tile_data_addr += ((int8_t)tile_id) * 16;
        }

        /* Every pixel is stored with 2 bits so in one byte there are 4 pixels. A row is 8 pixels so 2 bytes. */
        uint16_t tile_row_addr  = tile_data_addr + (world_y % 8) * 2; 

        /* The data is stored in two consecutive bytes, the first byte stores the least significant bit of the pixels 
           the second byte stores the most significant bit of the pixels
        */
        uint8_t byte1 = memory[tile_row_addr];
        uint8_t byte2 = memory[tile_row_addr+1];

        uint8_t bit_index = 7 - (world_x % 8);

        uint8_t color_bit1 = (byte2 >> bit_index) & 1;
        uint8_t color_bit0 = (byte1 >> bit_index) & 1;

        uint8_t color_number = (color_bit1 << 1) | color_bit0;

        /* Now based on the color number it is possible to get the right value from BG palette */
        uint8_t BGP = ReadMem(0xFF47);
        uint8_t color = (BGP >> (color_number * 2)) & 0x03;

        process_frame_buffer(x, ppu->ly, color);
    }
}

/* This function performs a step of an amount of clock cycles in the 
   PPU state machine. The purpose is to emulate correctly this behaviour 
   after the CPU has exectuted an instruction that takes an amount of time 
*/
void ppu_step(PPU *ppu, int cycles){
    ppu->cycle_counter += cycles;

    switch(ppu->mode){
        case MODE_2_OAM_SCAN:
            if(ppu->cycle_counter >= 80){
                ppu->cycle_counter -= 80;
                ppu_set_mode(ppu, MODE_3_DRAWING);
            }
            break;
        case MODE_3_DRAWING:
            if(ppu->cycle_counter >= 172){
                ppu->cycle_counter -= 172;
                ppu_set_mode(ppu, MODE_0_HBLANK);
                ppu_scanline(ppu);
            }
            break;
        case MODE_0_HBLANK:
            if (ppu->cycle_counter >= 204) {
                ppu->cycle_counter -= 204;
                ppu->ly++;
                memory[0xFF44] = ppu->ly;
                uint8_t LYC    = memory[0xFF45];
                uint8_t STAT   = memory[0xFF41];

                if(ppu->ly == LYC){
                    // Set coincidence Flag (second bit in stat)
                    memory[0xFF41] |= 0x04;
                    
                    // Check if the interrupt for this event is enabled (bit 6)
                    if((STAT & 0x40) != 0){
                        memory[0xFF0F] |= 0x02; // request STAT interrupt
                    } else{
                        memory[0xFF41] &= ~0x04;
                    }
                }

                if (ppu->ly == 144) {
                    ppu_set_mode(ppu, MODE_1_VBLANK);
                    
                    // Request V-Blank interrupt
                    memory[0xFF0F] |= 0x1; // Interrupt flag
                } else {
                    ppu_set_mode(ppu, MODE_2_OAM_SCAN);
                }
            }
            break;
        case MODE_1_VBLANK:
            if (ppu->cycle_counter >= 456) { // One scanline worth of time
                ppu->cycle_counter -= 456;
                ppu->ly++;
                memory[0xFF44] = ppu->ly;

                if (ppu->ly > 153) {
                    ppu->ly = 0;
                    memory[0xFF44] = 0;
                    ppu_set_mode(ppu, MODE_2_OAM_SCAN);
                }
            }
            break;
    }
}

/* This function updates timers for clock cycles executed */
void timer_step(TIMER *timer, int cycles){
    timer->div_cycle_counter   += cycles;
    timer->timer_cycle_counter += cycles;

    if(timer->div_cycle_counter >= (CLOCK_FREQ_HZ / DIV_INC_FREQ_HZ)){
        size_t increment = timer->div_cycle_counter / (CLOCK_FREQ_HZ / DIV_INC_FREQ_HZ);
        memory[0xFF04] += increment;
        timer->div_cycle_counter %= (CLOCK_FREQ_HZ / DIV_INC_FREQ_HZ);
    }

    uint8_t TAC = memory[0xFF07];
    if((TAC & 0x04) != 0){ // Enable = 1 so increment TIMA
        size_t tima_inc_rate;

        switch(TAC & 0x03){ // last 2 bits indicate the increment rate for TIMA
            case 0b00: tima_inc_rate = 4096;   break;
            case 0b01: tima_inc_rate = 262144; break;
            case 0b10: tima_inc_rate = 65536;  break;
            case 0b11: tima_inc_rate = 16384;  break;
        }

        if(timer->timer_cycle_counter >= (CLOCK_FREQ_HZ / tima_inc_rate)){
            size_t increment = timer->timer_cycle_counter / (CLOCK_FREQ_HZ / tima_inc_rate);
            size_t result = (uint16_t)memory[0xFF05] + increment;
            if(result > 0xFF){ // TIMA overflows so resets it to TMA value
                memory[0xFF05] = memory[0xFF06];
                // request interrupt
                memory[0xFF0F] |= 0x04;
            }
            else{
                memory[0xFF05] += increment;
            }
            timer->timer_cycle_counter %= (CLOCK_FREQ_HZ / tima_inc_rate);
        }
    }
}

/* Instruction function pointer type */
typedef int (*Instruction)(CPU *cpu);

/* Look-up table of function pointers for 8-bit instructions */
Instruction instruction_table[256];

/* Look-up table of function pointers for CB-prefixed instructions */
Instruction cb_instruction_table[256];


/* ---- FUNCTION POINTERS FOR OPCODES SECTION ---- */
int UNKNOWN(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC - 1);
    fprintf(stderr, "Error: Unknown opcode 0x%02X at address 0x%04X\n", opcode, cpu->PC - 1);
    // Stop the emulator
    cpu->running = 0; 
    return 0;
}

/* ---  GMB 8bit-Loadcommands --- */

/* This performs a store of a word contained in A register to memory 
   at the address contained in BC */
int LD_BCmem_A(CPU *cpu) {
    WriteMem(cpu->BC, (uint8_t)(cpu->AF >> 8));
    return 8;
}

/* This performs a store of a word contained in A register to memory 
   at the address contained in DE */
int LD_DEmem_A(CPU *cpu) {
    WriteMem(cpu->DE,(uint8_t)(cpu->AF >> 8));
    return 8;
}

/* This performs a store of a word contained in A register to memory 
   at the address contained in a 16-bit immediate value */
int LD_d16mem_A(CPU *cpu) {
    uint16_t addr = FetchWord(cpu);
    WriteMem(addr,(uint8_t)(cpu->AF >> 8));
    return 16;
}


/* This performs a store of a word contained in A register to memory 
   at the address contained in HL and increments it */
int LDI_HLmem_A(CPU * cpu){
    WriteMem(cpu->HL, (uint8_t)(cpu->AF >> 8));
    cpu->HL++;
    return 8;
}

/* This performs a store of a word contained in A register to memory 
   at the address contained in HL and decrements it */
int LDD_HLmem_A(CPU * cpu){
    WriteMem(cpu->HL, (uint8_t)(cpu->AF >> 8));
    cpu->HL--;
    return 8;
}

/* This performs a load into A of a word contained in memory 
   at the address stored in HL and increments it */
int LDI_A_HLmem(CPU * cpu){
    uint16_t value = (uint16_t)ReadMem(cpu->HL++);
    cpu->AF = (value << 8) | (cpu->AF & 0x00FF);
    return 8;
}

/* This performs a load into A of a word contained in memory 
   at the address stored in HL and decrements it */
int LDD_A_HLmem(CPU * cpu){
    uint16_t value = (uint16_t)ReadMem(cpu->HL--);
    cpu->AF = (value << 8) | (cpu->AF & 0x00FF);
    return 8;
}

/* This performs a load into A of a word contained in memory 
   at the address stored in BC register */
int LD_A_BCmem(CPU * cpu){
    uint16_t value = (uint16_t)ReadMem(cpu->BC);
    cpu->AF = (value << 8) | (cpu->AF & 0x00FF);
    return 8;
}

/* This performs a load into A of a word contained in memory 
   at the address stored in DE register */
int LD_A_DEmem(CPU * cpu){
    uint16_t value = (uint16_t)ReadMem(cpu->DE);
    cpu->AF = (value << 8) | (cpu->AF & 0x00FF);
    return 8;
}

/* This performs a load into A of a word contained in memory 
   at the address stored in an immediate 16 bit value */
int LD_A_d16mem(CPU * cpu){
    uint16_t addr = FetchWord(cpu);
    uint16_t value = (uint16_t)ReadMem(addr);
    cpu->AF = (value << 8) | (cpu->AF & 0x00FF);
    return 16;
}

/* Load data from a register r into another register r (opcodes 0x40-0x7F, except 0x76 that is HALT instruction) */
int LD_r_r(CPU *cpu) {
    uint8_t opcode = ReadMem(cpu->PC - 1);

    // Determine source and destination from the opcode
    uint8_t source_id = opcode & 0x07;
    uint8_t dest_id = (opcode >> 3) & 0x07;

    // --- Get the value 'n' from the source ---
    uint8_t n;
    switch (source_id) {
        case 0: n = (uint8_t)(cpu->BC >> 8);   break; // Register B
        case 1: n = (uint8_t)(cpu->BC & 0xFF); break; // Register C
        case 2: n = (uint8_t)(cpu->DE >> 8);   break; // Register D
        case 3: n = (uint8_t)(cpu->DE & 0xFF); break; // Register E
        case 4: n = (uint8_t)(cpu->HL >> 8);   break; // Register H
        case 5: n = (uint8_t)(cpu->HL & 0xFF); break; // Register L
        case 6: n = ReadMem(cpu->HL);           break; // Value from address (HL)
        case 7: n = (uint8_t)(cpu->AF >> 8);   break; // Register A
    }

    // --- Put the value 'n' into the destination ---
    switch (dest_id) {
        case 0: cpu->BC = (n << 8) | (cpu->BC & 0x00FF); break; // Register B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | n;        break; // Register C
        case 2: cpu->DE = (n << 8) | (cpu->DE & 0x00FF); break; // Register D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | n;        break; // Register E
        case 4: cpu->HL = (n << 8) | (cpu->HL & 0x00FF); break; // Register H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | n;        break; // Register L
        case 6: WriteMem(cpu->HL, n);                    break; // Address (HL)
        case 7: cpu->AF = (n << 8) | (cpu->AF & 0x00F0); break; // Register A
    }

    // Return cycle count (8 for memory access, 4 for registers)
    return (source_id == 6 || dest_id == 6) ? 8 : 4;
}

/* Load into memory at the immediate 16-bit address the SP value */
int LD_d16mem_SP(CPU *cpu){
    uint16_t addr = FetchWord(cpu);
    uint8_t sp_low = (uint8_t)(cpu->SP & 0x00FF);
    uint8_t sp_high = (uint8_t)(cpu->SP >> 8);

    WriteMem(addr, sp_low);
    WriteMem(addr + 1, sp_high);

    return 20; 
}

/* Load an immediate 8-bit value into a register r */
int LD_r_d8(CPU *cpu) {
    uint8_t opcode = ReadMem(cpu->PC - 1);

    // Fetch the immediate value d8 from mem
    uint8_t d8 = FetchByte(cpu);

    // --- Put the value 'n' into the destination ---
    switch (opcode) {
        case 0x06: cpu->BC = (d8 << 8) | (cpu->BC & 0x00FF); break; // Register B
        case 0x0E: cpu->BC = (cpu->BC & 0xFF00) | d8;        break; // Register C
        case 0x16: cpu->DE = (d8 << 8) | (cpu->DE & 0x00FF); break; // Register D
        case 0x1E: cpu->DE = (cpu->DE & 0xFF00) | d8;        break; // Register E
        case 0x26: cpu->HL = (d8 << 8) | (cpu->HL & 0x00FF); break; // Register H
        case 0x2E: cpu->HL = (cpu->HL & 0xFF00) | d8;        break; // Register L
        case 0x36: WriteMem(cpu->HL, d8);                    break; // Address (HL)
        case 0x3E: cpu->AF = (d8 << 8) | (cpu->AF & 0x00F0); break; // Register A
    }
    return opcode == 0x36 ? 12 : 8;
}

/* This reads from IO-port n into A register */
int LD_a8_A(CPU *cpu){
    uint8_t n = FetchByte(cpu);
    WriteMem(0xFF00+n, (uint8_t)(cpu->AF >> 8));
    return 12;
}

/* This writes to IO-port n from A register */
int LD_A_a8(CPU *cpu){
    uint8_t n = FetchByte(cpu);
    cpu->AF = (cpu->AF & 0x00FF) | ((uint16_t)(ReadMem(0xFF00 + n)) << 8);
    return 12;
}

/* This reads from IO-port in register C into A register */
int LD_A_Cmem(CPU *cpu){
    uint8_t c = (uint8_t)(cpu->BC & 0x00FF);
    cpu->AF = (cpu->AF & 0x00FF) | ((uint16_t)(ReadMem(0xFF00 + c)) << 8);
    return 8;
}

/* This write to IO-port in register C into A register */
int LD_Cmem_A(CPU *cpu){
    uint8_t c = (uint8_t)(cpu->BC & 0x00FF);
    WriteMem(0xFF00+c, (uint8_t)(cpu->AF >> 8));
    return 8;
}

/* --- GMB 8bit-Arithmetic/logical Commands --- */

/* Adds value stored in r register to the accumulator and stores the result there */
int ADD_A_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0x80: n = cpu->BC >> 8;      break; // Register B
        case 0x81: n = cpu->BC & 0xFF;    break; // Register C
        case 0x82: n = cpu->DE >> 8;      break; // Register D
        case 0x83: n = cpu->DE & 0xFF;    break; // Register E
        case 0x84: n = cpu->HL >> 8;      break; // Register H
        case 0x85: n = cpu->HL & 0xFF;    break; // Register L
        case 0x86: n = ReadMem(cpu->HL);  break; // Address (HL)
        case 0x87: n = cpu->AF >> 8;      break; // Register A
    }
    
    uint16_t result = n + a;

    cpu->AF &= 0xFF00; // Flags reset
    if((result & 0xFF) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return opcode == 0x86 ? 8 : 4;
}

/* Adds an immediate 8-bit value to the accumulator and stores the result there */
int ADD_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    
    uint16_t result = n + a;

    cpu->AF &= 0xFF00; // Flags reset
    if((result & 0xFF) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return 8;
}

/* Adds value stored in r register and carry to the accumulator and stores the result there */
int ADC_A_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint16_t n;
    uint16_t a = cpu->AF >> 8;
    uint16_t c = (cpu->AF & 0x0010) >> 4;
    

    switch (opcode){
        case 0x88: n = cpu->BC >> 8;     break; // Register B
        case 0x89: n = cpu->BC & 0xFF;   break; // Register C
        case 0x8A: n = cpu->DE >> 8;     break; // Register D
        case 0x8B: n = cpu->DE & 0xFF;   break; // Register E
        case 0x8C: n = cpu->HL >> 8;     break; // Register H
        case 0x8D: n = cpu->HL & 0xFF;   break; // Register L
        case 0x8E: n = ReadMem(cpu->HL); break; // Address (HL)
        case 0x8F: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = n + a + c;

    cpu->AF &= 0xFF00; // Flags reset
    if((result & 0xFF) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) + (c & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return opcode == 0x8E ? 8 : 4;
}

/* Adds an immediate 8-bit value and carry to the accumulator and stores the result there */
int ADC_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    uint16_t c = (cpu->AF & 0x0010) >> 4;
    
    uint16_t result = n + a + c;

    cpu->AF &= 0xFF00; // Flags reset
    if((result & 0xFF) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) + (c & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return 8;
}

/* Subtracts value stored in r register to the accumulator and stores the result there */
int SUB_A_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0x90: n = cpu->BC >> 8;     break; // Register B
        case 0x91: n = cpu->BC & 0xFF;   break; // Register C
        case 0x92: n = cpu->DE >> 8;     break; // Register D
        case 0x93: n = cpu->DE & 0xFF;   break; // Register E
        case 0x94: n = cpu->HL >> 8;     break; // Register H
        case 0x95: n = cpu->HL & 0xFF;   break; // Register L
        case 0x96: n = ReadMem(cpu->HL);  break; // Address (HL)
        case 0x97: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a - n;

    cpu->AF &= 0xFF00; // Flags reset
    cpu->AF |= 0x40;  // N flag set
    if((result & 0xFF) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F)) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(a < n) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return opcode == 0x96 ? 8 : 4;
}

/* Subtracts an immediate 8-bit value to the accumulator and stores the result there */
int SUB_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    
    uint16_t result = a - n;

    cpu->AF &= 0xFF00; // Flags reset
    cpu->AF |= 0x40;  // N flag set
    if((result & 0xFF) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F)) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(a < n) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

   cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return 8;
}

/* Subtracts value stored in r register and carry to the accumulator and stores the result there */
int SBC_A_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint16_t n;
    uint16_t a = cpu->AF >> 8;
    uint16_t c = (cpu->AF & 0x0010) >> 4;
    
    switch (opcode){
        case 0x98: n = cpu->BC >> 8;     break; // Register B
        case 0x99: n = cpu->BC & 0xFF;   break; // Register C
        case 0x9A: n = cpu->DE >> 8;     break; // Register D
        case 0x9B: n = cpu->DE & 0xFF;   break; // Register E
        case 0x9C: n = cpu->HL >> 8;     break; // Register H
        case 0x9D: n = cpu->HL & 0xFF;   break; // Register L
        case 0x9E: n = ReadMem(cpu->HL);  break; // Address (HL)
        case 0x9F: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a - n - c;

    cpu->AF &= 0xFF00; // Flags reset
    cpu->AF |= 0x40;  // N flag set
    if((result & 0xFF) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F) + c) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(a < n + c) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return opcode == 0x9E ? 8 : 4;
}

/* Subtracts an immediate 8-bit value and carry to the accumulator and stores the result there */
int SBC_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    uint16_t c = (cpu->AF & 0x0010) >> 4;
    
    uint16_t result = a - n - c;

    cpu->AF &= 0xFF00; // Flags reset
    cpu->AF |= 0x40;  // N flag set
    if((result & 0xFF) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F) + c) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(a < n + c) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return 8;
}

/* Does the logical AND between r register and the accumulator and stores the result there */
int AND_A_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0xA0: n = cpu->BC >> 8;     break; // Register B
        case 0xA1: n = cpu->BC & 0xFF;   break; // Register C
        case 0xA2: n = cpu->DE >> 8;     break; // Register D
        case 0xA3: n = cpu->DE & 0xFF;   break; // Register E
        case 0xA4: n = cpu->HL >> 8;     break; // Register H
        case 0xA5: n = cpu->HL & 0xFF;   break; // Register L
        case 0xA6: n = ReadMem(cpu->HL);  break; // Address (HL)
        case 0xA7: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a & n;

    cpu->AF &= 0xFF00; // Flags reset
    cpu->AF |= 0x20;  // H flag set
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return opcode == 0xA6 ? 8 : 4;
}
/* Does the logical AND between an immediate 8-bit value and the accumulator and stores the result there */
int AND_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    
    uint16_t result = a & n;

    cpu->AF &= 0xFF00; // Flags reset
    cpu->AF |= 0x20;  // H flag set
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return 8;
}

/* Does the logical OR between r register and the accumulator and stores the result there */
int OR_A_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0xB0: n = cpu->BC >> 8;     break; // Register B
        case 0xB1: n = cpu->BC & 0xFF;   break; // Register C
        case 0xB2: n = cpu->DE >> 8;     break; // Register D
        case 0xB3: n = cpu->DE & 0xFF;   break; // Register E
        case 0xB4: n = cpu->HL >> 8;     break; // Register H
        case 0xB5: n = cpu->HL & 0xFF;   break; // Register L
        case 0xB6: n = ReadMem(cpu->HL);  break; // Address (HL)
        case 0xB7: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a | n;

    cpu->AF &= 0xFF00; // Flags reset
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return opcode == 0xB6 ? 8 : 4;
}
/* Does the logical OR between an immediate 8-bit value and the accumulator and stores the result there */
int OR_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    
    uint16_t result = a | n;

    cpu->AF &= 0xFF00; // Flags reset
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return 8;
}

/* Does the logical XOR between r register and the accumulator and stores the result there */
int XOR_A_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0xA8: n = cpu->BC >> 8;     break; // Register B
        case 0xA9: n = cpu->BC & 0xFF;   break; // Register C
        case 0xAA: n = cpu->DE >> 8;     break; // Register D
        case 0xAB: n = cpu->DE & 0xFF;   break; // Register E
        case 0xAC: n = cpu->HL >> 8;     break; // Register H
        case 0xAD: n = cpu->HL & 0xFF;   break; // Register L
        case 0xAE: n = ReadMem(cpu->HL);  break; // Address (HL)
        case 0xAF: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a ^ n;

    cpu->AF &= 0xFF00; // Flags reset
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

   cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return opcode == 0xAE ? 8 : 4;
}
/* Does the logical XOR between an immediate 8-bit value and the accumulator and stores the result there */
int XOR_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    
    uint16_t result = a ^ n;

    cpu->AF &= 0xFF00; // Flags reset
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF = ((result & 0xFF) << 8) | (cpu->AF & 0x00F0);

    return 8;
}

/* Compares value stored in r register to the accumulator */
int CP_A_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0xB8: n = cpu->BC >> 8;     break; // Register B
        case 0xB9: n = cpu->BC & 0xFF;   break; // Register C
        case 0xBA: n = cpu->DE >> 8;     break; // Register D
        case 0xBB: n = cpu->DE & 0xFF;   break; // Register E
        case 0xBC: n = cpu->HL >> 8;     break; // Register H
        case 0xBD: n = cpu->HL & 0xFF;   break; // Register L
        case 0xBE: n = ReadMem(cpu->HL);  break; // Address (HL)
        case 0xBF: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a - n;

    cpu->AF &= 0xFF00; // Flags reset
    cpu->AF |= 0x40;  // N flag set
    if((result & 0xFF) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F)) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(a < n) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    return opcode == 0xBE ? 8 : 4;
}

/* Compares an immediate 8-b value to the accumulator */
int CP_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    
    uint16_t result = a - n;

    cpu->AF &= 0xFF00; // Flags reset
    cpu->AF |= 0x40;  // N flag set
    if((result & 0xFF) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F)) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;  

    if(a < n) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    return 8;
}

/* This increments the value in the 8bit register */
int INC_r(CPU *cpu) {
    uint8_t opcode = ReadMem(cpu->PC - 1);
    uint8_t dest_id = (opcode >> 3) & 0x07; 

    uint8_t value;

    switch (dest_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    uint8_t result = value + 1;

    if (result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF &= ~0x40; // Clear N flag

    if ((value & 0x0F) == 0x0F) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    switch (dest_id) {
        case 0: cpu->BC = (result << 8) | (cpu->BC & 0x00FF); break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | result;        break; // C
        case 2: cpu->DE = (result << 8) | (cpu->DE & 0x00FF); break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | result;        break; // E
        case 4: cpu->HL = (result << 8) | (cpu->HL & 0x00FF); break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | result;        break; // L
        case 6: WriteMem(cpu->HL, result);                    break; // (HL)
        case 7: cpu->AF = (result << 8) | (cpu->AF & 0x00F0); break; // A
    }
    
    return (dest_id == 6) ? 12 : 4;
}

/* This decrements the value in the 8bit register */
int DEC_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC - 1);
    uint8_t dest_id = (opcode >> 3) & 0x07; 

    uint8_t value;

    switch (dest_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    uint8_t result = value - 1;

    if (result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF |= 0x40; // N flag

    if ((value & 0x0F) == 0x00) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    switch (dest_id) {
        case 0: cpu->BC = (result << 8) | (cpu->BC & 0x00FF); break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | result;        break; // C
        case 2: cpu->DE = (result << 8) | (cpu->DE & 0x00FF); break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | result;        break; // E
        case 4: cpu->HL = (result << 8) | (cpu->HL & 0x00FF); break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | result;        break; // L
        case 6: WriteMem(cpu->HL, result);                    break; // (HL)
        case 7: cpu->AF = (result << 8) | (cpu->AF & 0x00F0); break; // A
    }
    
    return (dest_id == 6) ? 12 : 4;
}

/* Corrects the value into A for Binary Coded Decimal after an addition or a subtraction */
int DAA(CPU *cpu) {
    uint8_t a = (uint8_t)(cpu->AF >> 8);
    uint16_t result = a;
    
    bool n_flag = (cpu->AF & 0x40) != 0;
    bool h_flag = (cpu->AF & 0x20) != 0;
    bool c_flag = (cpu->AF & 0x10) != 0;

    if (n_flag) { // Last operation was subtraction
        if (h_flag) {
            result = (result - 0x06) & 0xFF;
        }
        if (c_flag) {
            result = (result - 0x60) & 0xFF;
        }
    } else { // Last operation was addition
        if (h_flag || (a & 0x0F) > 9) {
            result += 0x06;
        }
        if (c_flag || result > 0x99) {
            result += 0x60;
            cpu->AF |= 0x10; // Set carry flag
        }
    }

    // Set zero flag
    if ((result & 0xFF) == 0) {
        cpu->AF |= 0x80;
    } else {
        cpu->AF &= ~0x80;
    }

    // Clear half-carry flag (always cleared after DAA)
    cpu->AF &= ~0x20;
    
    // Update A register, preserve flags
    cpu->AF = (cpu->AF & 0x00FF) | ((result & 0xFF) << 8);

    return 4;
}

/* Complements the accumulator and sets H and N flags */
int CPL(CPU *cpu){
    cpu->AF = (((cpu->AF >> 8) ^ 0xFF) << 8) | (cpu->AF & 0x00FF); 

    cpu->AF |= 0x40; // N flag
    cpu->AF |= 0x20; // H flag
    return 4;
}

/* Sets the Carry flag to 1 and resets H and N */
int SCF(CPU *cpu){
    cpu->AF |= 0x10; // C flag
    cpu->AF &= ~0x20; // H flag
    cpu->AF &= ~0x40; // N flag
    return 4;
}

/* Complements the Carry flag and resets H and N */
int CCF(CPU *cpu){
    cpu->AF ^= 0x10; // C flag
    cpu->AF &= ~0x20; // H flag
    cpu->AF &= ~0x40; // N flag
    return 4;
}


/* ---  GMB 16bit --- */

/* This performs a load of a word into BC register */
int LD_BC_d16(CPU *cpu) {
    uint16_t value = FetchWord(cpu);
    cpu->BC = value;
    return 12; 
}

/* This performs a load of a word into DE register */
int LD_DE_d16(CPU *cpu) {
    uint16_t value = FetchWord(cpu);
    cpu->DE = value;
    return 12; 
}

/* This performs a load of a word into HL register */
int LD_HL_d16(CPU *cpu) {
    uint16_t value = FetchWord(cpu);
    cpu->HL = value;
    return 12; 
}

/* This performs a load of a word into SP register */
int LD_SP_d16(CPU *cpu) {
    uint16_t value = FetchWord(cpu);
    cpu->SP = value;
    return 12; 
}

/* This performs a copy of HL into SP */
int LD_SP_HL(CPU *cpu){
    cpu->SP = cpu->HL;
    return 8;
}

/* Load into HL the sum of SP with an immediate signed 8-bit value */
int LD_HL_SPs8(CPU *cpu){
    int8_t n = (int8_t)FetchByte(cpu);
    uint16_t result = cpu->SP + n;

    cpu->AF = 0x00; // Clear Z, N, H, C

    if( ((cpu->SP & 0x0F) + (n & 0x0F)) > 0x0F ) cpu->AF |= 0x20; // Half Carry
    if( ((cpu->SP & 0xFF) + (n & 0xFF)) > 0xFF ) cpu->AF |= 0x10; // Carry

    cpu->HL = result;
    return 12;
}

/* Push on the stack the value stored in BC register */
int PUSH_BC(CPU *cpu){
    WriteMem(--cpu->SP, (uint8_t)(cpu->BC >> 8));
    WriteMem(--cpu->SP, (uint8_t)(cpu->BC & 0x00FF));
    return 16;
}

/* Push on the stack the value stored in DE register */
int PUSH_DE(CPU *cpu){
    WriteMem(--cpu->SP, (uint8_t)(cpu->DE >> 8));
    WriteMem(--cpu->SP, (uint8_t)(cpu->DE & 0x00FF));
    return 16;
}

/* Push on the stack the value stored in HL register */
int PUSH_HL(CPU *cpu){
    WriteMem(--cpu->SP, (uint8_t)(cpu->HL >> 8));
    WriteMem(--cpu->SP, (uint8_t)(cpu->HL & 0x00FF));
    return 16;
}

/* Push on the stack the value stored in AF register */
int PUSH_AF(CPU *cpu){
    WriteMem(--cpu->SP, (uint8_t)(cpu->AF >> 8));
    WriteMem(--cpu->SP, (uint8_t)(cpu->AF & 0x00F0));
    return 16;
}

/* Pop from the stack a value and stores it in BC register */
int POP_BC(CPU *cpu){
    cpu->BC = (uint16_t)ReadMem(cpu->SP++);
    cpu->BC = ((cpu->BC & 0x00FF) | (uint16_t)ReadMem(cpu->SP++) << 8);
    return 12;
}

/* Pop from the stack a value and stores it in DE register */
int POP_DE(CPU *cpu){
    cpu->DE = (uint16_t)ReadMem(cpu->SP++);
    cpu->DE = ((cpu->DE & 0x00FF) | (uint16_t)ReadMem(cpu->SP++) << 8);
    return 12;
}
/* Pop from the stack a value and stores it in HL register */
int POP_HL(CPU *cpu){
    cpu->HL = (uint16_t)ReadMem(cpu->SP++);
    cpu->HL = ((cpu->HL & 0x00FF) | (uint16_t)ReadMem(cpu->SP++) << 8);
    return 12;
}
/* Pop from the stack a value and stores it in AF register */
int POP_AF(CPU *cpu){
    uint8_t flags = ReadMem(cpu->SP++);
    uint8_t a = ReadMem(cpu->SP++);
    cpu->AF = (uint16_t)(a << 8) | (flags & 0xF0);
    return 12;
}
/* ---------------------------------------- */

/* --- GMB 16-bit arithmetic and logic --- */

/* Increments a 16-bit register */
int INC_rr(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    switch (opcode){
        case 0x03: cpu->BC++; break;
        case 0x13: cpu->DE++; break;
        case 0x23: cpu->HL++; break;
        case 0x33: cpu->SP++; break;
    }
    return 8;
}

/* Decrements a 16-bit register */
int DEC_rr(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    switch (opcode){
        case 0x0B: cpu->BC--; break;
        case 0x1B: cpu->DE--; break;
        case 0x2B: cpu->HL--; break;
        case 0x3B: cpu->SP--; break;
    }
    return 8;
}

/* Adds to HL the value of a 16bit register */
int ADD_HL_rr(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);
    uint16_t n;
    switch(opcode){
        case 0x09: n = cpu->BC; break;
        case 0x19: n = cpu->DE; break;
        case 0x29: n = cpu->HL; break;
        case 0x39: n = cpu->SP; break; 
    }
    uint16_t result = cpu->HL + n;

    cpu->AF &= ~0x40; // Clear the N flag

    if(result > 0xFFFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    if((cpu->HL & 0x0FFF) + (n & 0x0FFF) > 0x0FFF) cpu->AF |= 0x20; // Half carry flag
    else cpu->AF &= ~0x20;

    cpu->HL = result;
    return 8;
}

/* Adds an immediate signed 8-bit value to SP */
int ADD_SP_s8(CPU *cpu){
    int8_t n = (int8_t)FetchByte(cpu);

    uint16_t result = cpu->SP + n;

   cpu->AF = 0x00; // Clear Z, N, H, C

    if( ((cpu->SP & 0x0F) + (n & 0x0F)) > 0x0F ) cpu->AF |= 0x20; // Half Carry
    if( ((cpu->SP & 0xFF) + (n & 0xFF)) > 0xFF ) cpu->AF |= 0x10; // Carry
    
    cpu->SP = result;
    return 16;
}


/* --- GMB Jumps/calls commands --- */

/* Jumps to an address represented by an immediate 16-bit value */
int JP_d16(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    cpu->PC = address;
    return 16;
}

/* Jumps to an address stored in HL register */
int JP_HL(CPU *cpu){
    cpu->PC = cpu->HL;
    return 4;
}

/* Jumps to an address represented by an immediate 16-bit value
   if and only if the zero flag is not set */
int JP_NZ_d16(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    if((cpu->AF & 0x0080) == 0x0000){ // Zero flag not set
        cpu->PC = address;
        return 16;
    }
    return 12;
}

/* Jumps to an address represented by an immediate 16-bit value
   if and only if the carry flag is not set */
int JP_NC_d16(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    if((cpu->AF & 0x0010) == 0x0000){ // Carry flag not set
        cpu->PC = address;
        return 16;
    }
    return 12;
}

/* Jumps to an address represented by an immediate 16-bit value
   if and only if the zero flag is set */
int JP_Z_d16(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    if((cpu->AF & 0x0080) == 0x0080){ // Zero flag set
        cpu->PC = address;
        return 16;
    }
    return 12;
}

/* Jumps to an address represented by an immediate 16-bit value
   if and only if the carry flag is set */
int JP_C_d16(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    if((cpu->AF & 0x0010) == 0x0010){ // Carry flag set
       cpu->PC = address;
        return 16;
    }
    return 12;
}

/* Relatively jumps with an immediate signed 8-bit value*/
int JR_d8(CPU *cpu){
    int8_t offset = (int8_t)FetchByte(cpu);
    cpu->PC += offset;
    return 12;
}

/* Relatively jumps with an offset represented by an immediate 8-bit value
   if and only if the zero flag is not set */
int JR_NZ_d8(CPU *cpu){
    int8_t offset = (int8_t)FetchByte(cpu);
    if((cpu->AF & 0x0080) == 0x0000){ // Zero flag not set
        cpu->PC += offset;
        return 12;
    }
    return 8;
}

/* Relatively jumps with an offset represented by an immediate 8-bit value
   if and only if the carry flag is not set */
int JR_NC_d8(CPU *cpu){
    int8_t offset = (int8_t)FetchByte(cpu);
    if((cpu->AF & 0x0010) == 0x0000){ // Carry flag not set
        cpu->PC += offset;
        return 12;
    }
    return 8;
}

/* Relatively jumps with an offset represented by an immediate 8-bit value
   if and only if the zero flag is set */
int JR_Z_d8(CPU *cpu){
    int8_t offset = (int8_t)FetchByte(cpu);
    if((cpu->AF & 0x0080) == 0x0080){ // Zero flag set
        cpu->PC += offset;
        return 12;
    }
    return 8;
}

/* Relatively jumps with an offset represented by an immediate 8-bit value
   if and only if the carry flag is set */
int JR_C_d8(CPU *cpu){
    int8_t offset = (int8_t)FetchByte(cpu);
    if((cpu->AF & 0x0010) == 0x0010){ // Carry flag set
        cpu->PC += offset;
        return 12;
    }
    return 8;
}

/* Calls a procedure saving program counter */
int CALL(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    cpu->SP -= 2;
    WriteMem(cpu->SP, (uint8_t)(cpu->PC & 0xFF));
    WriteMem(cpu->SP + 1, (uint8_t)(cpu->PC >> 8));
    cpu->PC = address;
    return 24; 
}

/* Calls a procedure saving program counter 
   if and only if zero flag is not set
 */
int CALL_NZ(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    if((cpu->AF & 0x0080) == 0x0000){ // Zero flag not set
        cpu->SP -= 2;
        WriteMem(cpu->SP, (uint8_t)(cpu->PC & 0xFF));
        WriteMem(cpu->SP + 1, (uint8_t)(cpu->PC >> 8));
        cpu->PC = address;
        return 24;
    }
    return 12;
}

/* Calls a procedure saving program counter 
   if and only if zero flag is set
 */
int CALL_Z(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    if((cpu->AF & 0x0080) == 0x0080){ // Zero flag set
        cpu->SP -= 2;
        WriteMem(cpu->SP, (uint8_t)(cpu->PC & 0xFF));
        WriteMem(cpu->SP + 1, (uint8_t)(cpu->PC >> 8));
        cpu->PC = address;
        return 24;
    }
    return 12;
}

/* Calls a procedure saving program counter 
   if and only if carry flag is not set
 */
int CALL_NC(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    if((cpu->AF & 0x0010) == 0x0000){ // Carry flag not set
        cpu->SP -= 2;
        WriteMem(cpu->SP, (uint8_t)(cpu->PC & 0xFF));
        WriteMem(cpu->SP + 1, (uint8_t)(cpu->PC >> 8));
        cpu->PC = address;
        return 24;
    }
    return 12;
}

/* Calls a procedure saving program counter 
   if and only if carry flag is set
 */
int CALL_C(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    if((cpu->AF & 0x0010) == 0x0010){ // Carry flag set
        cpu->SP -= 2;
        WriteMem(cpu->SP, (uint8_t)(cpu->PC & 0xFF));
        WriteMem(cpu->SP + 1, (uint8_t)(cpu->PC >> 8));
        cpu->PC = address;
        return 24;
    }
    return 12;
}

/* Returns restoring previous program counter */
int RET(CPU *cpu){
    uint8_t lo = ReadMem(cpu->SP);
    uint8_t hi = ReadMem(cpu->SP + 1);
    cpu->SP += 2;
    cpu->PC = (uint16_t)(hi << 8) | lo;
    return 16;
}

/* Returns restoring previous program caounter 
   and enables interrupts */
int RETI(CPU *cpu){
    RET(cpu);
    cpu->IME = true;
    return 16;
}


/* Returns restoring previous program counter
   if and only if zero flag is not set
 */
int RET_NZ(CPU *cpu){
    if((cpu->AF & 0x0080) == 0x0000){ // Zero flag not set
        RET(cpu);
        return 20;
    }
    return 8;
}

/* Returns restoring previous program counter
   if and only if zero flag is set
 */
int RET_Z(CPU *cpu){
    if((cpu->AF & 0x0080) == 0x0080){ // Zero flag set
        RET(cpu);
        return 20;
    }
    return 8;
}

/* Returns restoring previous program counter
   if and only if carry flag is not set
 */
int RET_NC(CPU *cpu){
    if((cpu->AF & 0x0010) == 0x0000){ // Carry flag not set
        RET(cpu);
        return 20;
    }
    return 8;
}

/* Returns restoring previous program counter
   if and only if carry flag is set
 */
int RET_C(CPU *cpu){
    if((cpu->AF & 0x0010) == 0x0010){ // Carry flag set
        RET(cpu);
        return 20;
    }
    return 8;
}

/* One byte long call instruction to hardcoded addresses */
int RST(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC -1);
    uint16_t address;

    switch(opcode){
        case 0xC7: address = 0x00; break;
        case 0xCF: address = 0x08; break;
        case 0xD7: address = 0x10; break;
        case 0xDF: address = 0x18; break;
        case 0xE7: address = 0x20; break;
        case 0xEF: address = 0x28; break;
        case 0xF7: address = 0x30; break;
        case 0xFF: address = 0x38; break;
    }

    cpu->SP -= 2;
    WriteMem(cpu->SP, (uint8_t)(cpu->PC & 0xFF));
    WriteMem(cpu->SP + 1, (uint8_t)(cpu->PC >> 8));
    cpu->PC = address;
    return 16; 
}

/* --- GMB CPU-Controlcommands --- */

/* This performs a no-operation on the cpu*/
int NOP(CPU *cpu){ return 4; }

/* This performs the HALT instruction */
int HALT(CPU *cpu){ 
    // HALT bug
   /* if(!cpu->IME ){
        uint8_t IE = memory[0xFF0F];
        uint8_t IF = memory[0xFFFF];
        if((IE & IF) != 0) cpu->PC--;
    }*/
    cpu->halted = true;
    return 4;    
}

int STOP(CPU *cpu){
    /* The STOP instruction is two bytes long. The 0x10 has already been
       fetched, so fetch the following 0x00 byte. */
    FetchByte(cpu);

    memory[0xFF04] = 0; // resets DIV register 
    uint8_t TAC = memory[0xFF07];

    if((TAC & 0x0040) != 0){ // Enable = 1 increment TIMA

    }

    // TODO CHANGE AFTER PPU AND JOYPAD
    cpu->halted = true;
    return 4;
}

/* Disables interrupts */
int DI(CPU *cpu){
    cpu->IME = false;
    return 4;
}

/* Enables interrupts */
int EI(CPU *cpu){
    cpu->IME = true;
    return 4;
}

int handle_cb_prefix(CPU *cpu){
    uint8_t opcode = FetchByte(cpu);
    return 4 + cb_instruction_table[opcode](cpu);
}

/* --- GMB Rotate/Shift commands --- */

/* Rotates the bits of the A register one position to the left in a circular fashion */
int RLCA(CPU *cpu){
    cpu->AF &= 0xFF00; // Flags reset
    cpu->AF |= (cpu->AF & 0x8000) >> 11; // New carry stored based on most sign. bit of A
    
    uint16_t new_a = ((cpu->AF << 1) & 0xFF00) >> 8; // shift A by one
    new_a |= (cpu->AF & 0x8000) >> 15;  // add the carry as least sign. bit

    cpu->AF = (new_a << 8) | (cpu->AF & 0x00FF); 
    return 4;
}

/* Rotates the bits of the A register one position to the left through carry bit */
int RLA(CPU *cpu){
    cpu->AF &= 0xFF10; // Flags reset avoiding carry

    bool was_carry_set = (cpu->AF & 0x10) != 0;
    cpu->AF |= (cpu->AF & 0x8000) >> 11; // New carry stored based on most sign. bit of A
    uint16_t new_a = ((cpu->AF << 1) & 0xFF00) >> 8; // shift A by one

    if(was_carry_set){
        new_a |= 0x0001;  // add the carry as least sign. bit
    }

    cpu->AF = (new_a << 8) | (cpu->AF & 0x00FF); 
    return 4;
}

/* Rotates the bits of the A register one position to the right in a circular fashion */
int RRCA(CPU *cpu){
    cpu->AF &= 0xFF00; // Flags reset
    cpu->AF |= (cpu->AF & 0x0100) >> 4; // New carry stored based on least sign. bit of A
    
    uint16_t new_a = ((cpu->AF >> 1) & 0xFF00) >> 8; // shift A by one
    new_a |= (cpu->AF & 0x0100) >> 4;  // add the carry as most sign. bit

    cpu->AF = (new_a << 8) | (cpu->AF & 0x00FF); 
    return 4;
}

/* Rotates the bits of the A register one position to the right through carry bit */
int RRA(CPU *cpu){
    cpu->AF &= 0xFF10; // Flags reset avoiding carry

    bool was_carry_set = (cpu->AF & 0x10) != 0;
    cpu->AF |= (cpu->AF & 0x0100) >> 4; // New carry stored based on least sign. bit of A
    uint16_t new_a = ((cpu->AF >> 1) & 0xFF00) >> 8; // shift A by one

    if(was_carry_set){
        new_a |= 0x0080;  // add the carry as most sign. bit
    }

    cpu->AF = (new_a << 8) | (cpu->AF & 0x00FF); 
    return 4;
}

/* Rotates left the value stored in register r */
int RLC_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);
    uint8_t source_id = opcode & 0x07;
    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    bool carry_set = false;
    cpu->AF &= 0xFF00; // Clear flags
    if((value >> 7) == 1){ // Most sign. bit before rotation is 1
        cpu->AF |= 0x10; // Set the new carry flag
        carry_set = true;
        
    }

    value = value << 1;

    if(carry_set) value |= 0x01;
    if(value == 0) cpu->AF |= 0x80; // Zero flag

    switch (source_id) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return source_id == 6 ? 16 : 8;
}

/* Rotates right the value stored in register r */
int RRC_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    bool carry_set = false;
    cpu->AF &= 0xFF00; // Clear flags
    if((value & 0x01) == 1){ // Least sign. bit before rotation is 1
        cpu->AF |= 0x10; // Set the new carry flag
        carry_set = true;
        
    }

    value = value >> 1;

    if(carry_set) value |= 0x80;
    if(value == 0) cpu->AF |= 0x80; // Zero flag

    switch (source_id) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return source_id == 6 ? 16 : 8;
}

/* Rotates left through carry the value stored in register r */
int RL_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    bool was_carry_set = (cpu->AF & 0x10) != 0;

    cpu->AF &= 0xFF00; // Clear flags
    if((value >> 7) == 1){ // Most sign. bit before rotation is 1
        cpu->AF |= 0x10; // Set the new carry flag
    }

    value = value << 1;

    if(was_carry_set) value |= 0x01; // Old carry value as least sign. bit
    if(value == 0) cpu->AF |= 0x80; // Zero flag

    switch (source_id) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return source_id == 6 ? 16 : 8;
}


/* Rotates right through carry the value stored in register r */
int RR_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    bool was_carry_set = (cpu->AF & 0x10) != 0;

    cpu->AF &= 0xFF00; // Clear flags
    if((value & 0x01) == 1){ // Least sign. bit before rotation is 1
        cpu->AF |= 0x10; // Set the new carry flag
    }

    value = value >> 1;

    if(was_carry_set) value |= 0x80; // Old carry value as most sign. bit
    if(value == 0) cpu->AF |= 0x80; // Zero flag

    switch (source_id) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return source_id == 6 ? 16 : 8;
}

/* Shifts one position to left. The most significant bit goes into carry */
int SLA_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    cpu->AF &= 0xFF00; // Clear flags
    if((value >> 7) == 1){ // Most sign. bit before shift is 1
        cpu->AF |= 0x10; // Set the new carry flag
    }

    value = value << 1;
    if(value == 0) cpu->AF |= 0x80; // Zero flag

    switch (source_id) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return source_id == 6 ? 16 : 8;
}

/* Shifts arithmetical one position to right. The least significant bit goes into carry */
int SRA_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    cpu->AF &= 0xFF00; // Clear flags
    if((value & 0x01) == 1){ // Least sign. bit before shift is 1
        cpu->AF |= 0x10; // Set the new carry flag     
    }

    uint8_t bit7 = value & 0x80; // Save the original sign bit
    value = value >> 1;
    value |= bit7; // apply the original sign bit

    if(value == 0) cpu->AF |= 0x80; // Zero flag

    switch (source_id) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return source_id == 6 ? 16 : 8;
}

/* Shifts logical one position to right. The least significant bit goes into carry */
int SRL_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    cpu->AF &= 0xFF00; // Clear flags
    if((value & 0x01) == 1){ // Least sign. bit before shift is 1
        cpu->AF |= 0x10; // Set the new carry flag     
    }

    value = value >> 1;

    if(value == 0) cpu->AF |= 0x80; // Zero flag

    switch (source_id) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return source_id == 6 ? 16 : 8;
}

/* Swaps the high and low nibbles of a byte contained in a register */
int SWAP_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    uint8_t high_nibble = value >> 4;
    uint8_t low_nibble  = value &0x0F;

    value = (low_nibble << 4) | high_nibble;

    cpu->AF &= 0xFF00; // Clear flags
    if(value == 0) cpu->AF |= 0x80; // Zero flag

    switch (source_id) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return source_id == 6 ? 16 : 8;

}

/* Tests if the n th bit of a register is zero */
int BIT_n_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint8_t n = (opcode >> 3) & 0x07;
    uint8_t source_id = opcode & 0x07;

    uint8_t value;
    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    cpu->AF &= ~0x40; // Clear N flag
    cpu->AF |= 0x20;  // Set H flag

    if((value & (1 << n)) == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80; 

    return source_id == 6 ? 12 : 8; 
}

/* Set the n th bit of a register to 1 */
int SET_n_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint8_t n = (opcode >> 3) & 0x07;
    uint8_t source_id = opcode & 0x07;

    uint8_t value;
    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    value |= (1 << n);
    
    switch (source_id) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return source_id == 6 ? 16 : 8;
}

/* Sets the n th bit of a register to 0 */
int RES_n_r(CPU *cpu){
    uint8_t opcode = ReadMem(cpu->PC-1);

    uint8_t n = (opcode >> 3) & 0x07;
    uint8_t source_id = opcode & 0x07;

    uint8_t value;
    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = ReadMem(cpu->HL);           break; // (HL)
        case 7: value = (uint8_t)(cpu->AF >> 8);   break; // A
    }

    value &= ~(1 << n);
    
    switch (source_id) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return source_id == 6 ? 16 : 8;
}

/* This function allows the cpu to correctly handle interrupts */
int handleInterrupts(CPU *cpu){
    if(!cpu->IME){
        return 0;
    }

    uint8_t IE = ReadMem(0xFFFF);
    uint8_t IF = ReadMem(0xFF0F);

    uint8_t requested = IE & IF;

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
        memory[0xFF0F] &= ~0x01; // Clear the request flag
        cpu->PC = 0x0040;
    } else if (requested & 0x02) { // LCD STAT
        memory[0xFF0F] &= ~0x02;
        cpu->PC = 0x0048;
    } else if (requested & 0x04) { // Timer
        memory[0xFF0F] &= ~0x04;
        cpu->PC = 0x0050;
    } else if (requested & 0x08) { // Serial
        memory[0xFF0F] &= ~0x08;
        cpu->PC = 0x0058;
    } else if (requested & 0x10) { // Joypad
        memory[0xFF0F] &= ~0x10;
        cpu->PC = 0x0060;
    }

    return 20;
}



/* utility function to initialize instruction table */
void InitializeInstructionTable(){
    for(int i = 0; i<256; i++){
        instruction_table[i] = UNKNOWN;
    }

    for(int i = 0; i<256; i++){
        cb_instruction_table[i] = UNKNOWN;
    }

    instruction_table[0x00] = NOP;
    

    instruction_table[0x01] = LD_BC_d16;
    instruction_table[0x11] = LD_DE_d16;
    instruction_table[0x21] = LD_HL_d16;
    instruction_table[0x31] = LD_SP_d16;

    instruction_table[0x02] = LD_BCmem_A;
    instruction_table[0x12] = LD_DEmem_A;
    instruction_table[0x22] = LDI_HLmem_A;
    instruction_table[0x32] = LDD_HLmem_A;


    instruction_table[0x06] = LD_r_d8;
    instruction_table[0x16] = LD_r_d8;
    instruction_table[0x26] = LD_r_d8;
    instruction_table[0x36] = LD_r_d8;
    instruction_table[0x0E] = LD_r_d8;
    instruction_table[0x1E] = LD_r_d8;
    instruction_table[0x2E] = LD_r_d8;
    instruction_table[0x3E] = LD_r_d8;

    instruction_table[0x0A] = LD_A_BCmem;
    instruction_table[0x1A] = LD_A_DEmem;
    instruction_table[0x2A] = LDI_A_HLmem;
    instruction_table[0x3A] = LDD_A_HLmem;

    instruction_table[0x40] = LD_r_r;
    instruction_table[0x41] = LD_r_r;
    instruction_table[0x42] = LD_r_r;
    instruction_table[0x43] = LD_r_r;
    instruction_table[0x44] = LD_r_r;
    instruction_table[0x45] = LD_r_r;
    instruction_table[0x46] = LD_r_r;
    instruction_table[0x47] = LD_r_r;

    instruction_table[0x48] = LD_r_r;
    instruction_table[0x49] = LD_r_r;
    instruction_table[0x4A] = LD_r_r;
    instruction_table[0x4B] = LD_r_r;
    instruction_table[0x4C] = LD_r_r;
    instruction_table[0x4D] = LD_r_r;
    instruction_table[0x4E] = LD_r_r;
    instruction_table[0x4F] = LD_r_r;

    instruction_table[0x50] = LD_r_r;
    instruction_table[0x51] = LD_r_r;
    instruction_table[0x52] = LD_r_r;
    instruction_table[0x53] = LD_r_r;
    instruction_table[0x54] = LD_r_r;
    instruction_table[0x55] = LD_r_r;
    instruction_table[0x56] = LD_r_r;
    instruction_table[0x57] = LD_r_r;

    instruction_table[0x58] = LD_r_r;
    instruction_table[0x59] = LD_r_r;
    instruction_table[0x5A] = LD_r_r;
    instruction_table[0x5B] = LD_r_r;
    instruction_table[0x5C] = LD_r_r;
    instruction_table[0x5D] = LD_r_r;
    instruction_table[0x5E] = LD_r_r;
    instruction_table[0x5F] = LD_r_r;

    instruction_table[0x60] = LD_r_r;
    instruction_table[0x61] = LD_r_r;
    instruction_table[0x62] = LD_r_r;
    instruction_table[0x63] = LD_r_r;
    instruction_table[0x64] = LD_r_r;
    instruction_table[0x65] = LD_r_r;
    instruction_table[0x66] = LD_r_r;
    instruction_table[0x67] = LD_r_r;

    instruction_table[0x68] = LD_r_r;
    instruction_table[0x69] = LD_r_r;
    instruction_table[0x6A] = LD_r_r;
    instruction_table[0x6B] = LD_r_r;
    instruction_table[0x6C] = LD_r_r;
    instruction_table[0x6D] = LD_r_r;
    instruction_table[0x6E] = LD_r_r;
    instruction_table[0x6F] = LD_r_r;

    instruction_table[0x70] = LD_r_r;
    instruction_table[0x71] = LD_r_r;
    instruction_table[0x72] = LD_r_r;
    instruction_table[0x73] = LD_r_r;
    instruction_table[0x74] = LD_r_r;
    instruction_table[0x75] = LD_r_r;
    instruction_table[0x76] = HALT;
    instruction_table[0x77] = LD_r_r;

    instruction_table[0x78] = LD_r_r;
    instruction_table[0x79] = LD_r_r;
    instruction_table[0x7A] = LD_r_r;
    instruction_table[0x7B] = LD_r_r;
    instruction_table[0x7C] = LD_r_r;
    instruction_table[0x7D] = LD_r_r;
    instruction_table[0x7E] = LD_r_r;
    instruction_table[0x7F] = LD_r_r;

    instruction_table[0xEA] = LD_d16mem_A;
    instruction_table[0xFA] = LD_A_d16mem;

    instruction_table[0x08] = LD_d16mem_SP;

    instruction_table[0xE0] = LD_a8_A;
    instruction_table[0xF0] = LD_A_a8;

    instruction_table[0xF2] = LD_A_Cmem;
    instruction_table[0xE2] = LD_Cmem_A;

    instruction_table[0xF9] = LD_SP_HL;
    instruction_table[0xF8] = LD_HL_SPs8;

    instruction_table[0xC5] = PUSH_BC;
    instruction_table[0xD5] = PUSH_DE;
    instruction_table[0xE5] = PUSH_HL;
    instruction_table[0xF5] = PUSH_AF;

    instruction_table[0xC1] = POP_BC;
    instruction_table[0xD1] = POP_DE;
    instruction_table[0xE1] = POP_HL;
    instruction_table[0xF1] = POP_AF;

    instruction_table[0x80] = ADD_A_r;
    instruction_table[0x81] = ADD_A_r;
    instruction_table[0x82] = ADD_A_r;
    instruction_table[0x83] = ADD_A_r;
    instruction_table[0x84] = ADD_A_r;
    instruction_table[0x85] = ADD_A_r;
    instruction_table[0x86] = ADD_A_r;
    instruction_table[0x87] = ADD_A_r;

    instruction_table[0x88] = ADC_A_r;
    instruction_table[0x89] = ADC_A_r;
    instruction_table[0x8A] = ADC_A_r;
    instruction_table[0x8B] = ADC_A_r;
    instruction_table[0x8C] = ADC_A_r;
    instruction_table[0x8D] = ADC_A_r;
    instruction_table[0x8E] = ADC_A_r;
    instruction_table[0x8F] = ADC_A_r;

    instruction_table[0x90] = SUB_A_r;
    instruction_table[0x91] = SUB_A_r;
    instruction_table[0x92] = SUB_A_r;
    instruction_table[0x93] = SUB_A_r;
    instruction_table[0x94] = SUB_A_r;
    instruction_table[0x95] = SUB_A_r;
    instruction_table[0x96] = SUB_A_r;
    instruction_table[0x97] = SUB_A_r;

    instruction_table[0x98] = SBC_A_r;
    instruction_table[0x99] = SBC_A_r;
    instruction_table[0x9A] = SBC_A_r;
    instruction_table[0x9B] = SBC_A_r;
    instruction_table[0x9C] = SBC_A_r;
    instruction_table[0x9D] = SBC_A_r;
    instruction_table[0x9E] = SBC_A_r;
    instruction_table[0x9F] = SBC_A_r;

    instruction_table[0xA0] = AND_A_r;
    instruction_table[0xA1] = AND_A_r;
    instruction_table[0xA2] = AND_A_r;
    instruction_table[0xA3] = AND_A_r;
    instruction_table[0xA4] = AND_A_r;
    instruction_table[0xA5] = AND_A_r;
    instruction_table[0xA6] = AND_A_r;
    instruction_table[0xA7] = AND_A_r;

    instruction_table[0xA8] = XOR_A_r;
    instruction_table[0xA9] = XOR_A_r;
    instruction_table[0xAA] = XOR_A_r;
    instruction_table[0xAB] = XOR_A_r;
    instruction_table[0xAC] = XOR_A_r;
    instruction_table[0xAD] = XOR_A_r;
    instruction_table[0xAE] = XOR_A_r;
    instruction_table[0xAF] = XOR_A_r;

    instruction_table[0xB0] = OR_A_r;
    instruction_table[0xB1] = OR_A_r;
    instruction_table[0xB2] = OR_A_r;
    instruction_table[0xB3] = OR_A_r;
    instruction_table[0xB4] = OR_A_r;
    instruction_table[0xB5] = OR_A_r;
    instruction_table[0xB6] = OR_A_r;
    instruction_table[0xB7] = OR_A_r;

    instruction_table[0xB8] = CP_A_r;
    instruction_table[0xB9] = CP_A_r;
    instruction_table[0xBA] = CP_A_r;
    instruction_table[0xBB] = CP_A_r;
    instruction_table[0xBC] = CP_A_r;
    instruction_table[0xBD] = CP_A_r;
    instruction_table[0xBE] = CP_A_r;
    instruction_table[0xBF] = CP_A_r;

    instruction_table[0xC6] = ADD_A_d8;
    instruction_table[0xD6] = SUB_A_d8;
    instruction_table[0xE6] = AND_A_d8;
    instruction_table[0xF6] = OR_A_d8;

    instruction_table[0xCE] = ADC_A_d8;
    instruction_table[0xDE] = SBC_A_d8;
    instruction_table[0xEE] = XOR_A_d8;
    instruction_table[0xFE] = CP_A_d8;

    instruction_table[0x04] = INC_r;
    instruction_table[0x14] = INC_r;
    instruction_table[0x24] = INC_r;
    instruction_table[0x34] = INC_r;
    instruction_table[0x0C] = INC_r;
    instruction_table[0x1C] = INC_r;
    instruction_table[0x2C] = INC_r;
    instruction_table[0x3C] = INC_r;

    instruction_table[0x05] = DEC_r;
    instruction_table[0x15] = DEC_r;
    instruction_table[0x25] = DEC_r;
    instruction_table[0x35] = DEC_r;
    instruction_table[0x0D] = DEC_r;
    instruction_table[0x1D] = DEC_r;
    instruction_table[0x2D] = DEC_r;
    instruction_table[0x3D] = DEC_r;

    instruction_table[0x27] = DAA;
    instruction_table[0x2F] = CPL;

    instruction_table[0x37] = SCF;
    instruction_table[0x3F] = CCF;

    instruction_table[0x03] = INC_rr;
    instruction_table[0x13] = INC_rr;
    instruction_table[0x23] = INC_rr;
    instruction_table[0x33] = INC_rr;

    instruction_table[0x09] = ADD_HL_rr;
    instruction_table[0x19] = ADD_HL_rr;
    instruction_table[0x29] = ADD_HL_rr;
    instruction_table[0x39] = ADD_HL_rr;

    instruction_table[0x0B] = DEC_rr;
    instruction_table[0x1B] = DEC_rr;
    instruction_table[0x2B] = DEC_rr;
    instruction_table[0x3B] = DEC_rr;

    instruction_table[0xE8] = ADD_SP_s8;

    instruction_table[0xC3] = JP_d16;
    instruction_table[0xE9] = JP_HL;

    instruction_table[0xC2] = JP_NZ_d16;
    instruction_table[0xD2] = JP_NC_d16;
    instruction_table[0xCA] = JP_Z_d16;
    instruction_table[0xDA] = JP_C_d16;

    instruction_table[0x18] = JR_d8;
    instruction_table[0x28] = JR_Z_d8;
    instruction_table[0x38] = JR_C_d8;
    instruction_table[0x20] = JR_NZ_d8;
    instruction_table[0x30] = JR_NC_d8;

    instruction_table[0xCD] = CALL;

    instruction_table[0xCC] = CALL_Z;
    instruction_table[0xC4] = CALL_NZ;
    instruction_table[0xD4] = CALL_NC;
    instruction_table[0xDC] = CALL_C;

    instruction_table[0xC9] = RET;

    instruction_table[0xC0] = RET_NZ;
    instruction_table[0xC8] = RET_Z;
    instruction_table[0xD0] = RET_NC;
    instruction_table[0xD8] = RET_C;

    instruction_table[0xD9] = RETI;

    instruction_table[0xC7] = RST;
    instruction_table[0xCF] = RST;
    instruction_table[0xD7] = RST;
    instruction_table[0xDF] = RST;
    instruction_table[0xE7] = RST;
    instruction_table[0xEF] = RST;
    instruction_table[0xF7] = RST;
    instruction_table[0xFF] = RST;
    

    instruction_table[0xF3] = DI;
    instruction_table[0xFB] = EI;
    instruction_table[0x10] = STOP;

    instruction_table[0x07] = RLCA;
    instruction_table[0x17] = RLA;
    instruction_table[0x0F] = RRCA;
    instruction_table[0x1F] = RRA;
    
    instruction_table[0xCB] = handle_cb_prefix;

    // Map all illegal opcodes to NOP
    instruction_table[0xD3] = NOP;
    instruction_table[0xDB] = NOP;
    instruction_table[0xDD] = NOP;
    instruction_table[0xE3] = NOP;
    instruction_table[0xE4] = NOP;
    instruction_table[0xEB] = NOP;
    instruction_table[0xEC] = NOP;
    instruction_table[0xED] = NOP;
    instruction_table[0xF4] = NOP;
    instruction_table[0xFC] = NOP;
    instruction_table[0xFD] = NOP;

    // ------ CB prefixed instruction table ------ //

    cb_instruction_table[0x00] = RLC_r;
    cb_instruction_table[0x01] = RLC_r;
    cb_instruction_table[0x02] = RLC_r;
    cb_instruction_table[0x03] = RLC_r;
    cb_instruction_table[0x04] = RLC_r;
    cb_instruction_table[0x05] = RLC_r;
    cb_instruction_table[0x06] = RLC_r;
    cb_instruction_table[0x07] = RLC_r;

    cb_instruction_table[0x08] = RRC_r;
    cb_instruction_table[0x09] = RRC_r;
    cb_instruction_table[0x0A] = RRC_r;
    cb_instruction_table[0x0B] = RRC_r;
    cb_instruction_table[0x0C] = RRC_r;
    cb_instruction_table[0x0D] = RRC_r;
    cb_instruction_table[0x0E] = RRC_r;
    cb_instruction_table[0x0F] = RRC_r;

    cb_instruction_table[0x10] = RL_r;
    cb_instruction_table[0x11] = RL_r;
    cb_instruction_table[0x12] = RL_r;
    cb_instruction_table[0x13] = RL_r;
    cb_instruction_table[0x14] = RL_r;
    cb_instruction_table[0x15] = RL_r;
    cb_instruction_table[0x16] = RL_r;
    cb_instruction_table[0x17] = RL_r;

    cb_instruction_table[0x18] = RR_r;
    cb_instruction_table[0x19] = RR_r;
    cb_instruction_table[0x1A] = RR_r;
    cb_instruction_table[0x1B] = RR_r;
    cb_instruction_table[0x1C] = RR_r;
    cb_instruction_table[0x1D] = RR_r;
    cb_instruction_table[0x1E] = RR_r;
    cb_instruction_table[0x1F] = RR_r;

    cb_instruction_table[0x20] = SLA_r;
    cb_instruction_table[0x21] = SLA_r;
    cb_instruction_table[0x22] = SLA_r;
    cb_instruction_table[0x23] = SLA_r;
    cb_instruction_table[0x24] = SLA_r;
    cb_instruction_table[0x25] = SLA_r;
    cb_instruction_table[0x26] = SLA_r;
    cb_instruction_table[0x27] = SLA_r;

    cb_instruction_table[0x28] = SRA_r;
    cb_instruction_table[0x29] = SRA_r;
    cb_instruction_table[0x2A] = SRA_r;
    cb_instruction_table[0x2B] = SRA_r;
    cb_instruction_table[0x2C] = SRA_r;
    cb_instruction_table[0x2D] = SRA_r;
    cb_instruction_table[0x2E] = SRA_r;
    cb_instruction_table[0x2F] = SRA_r;

    cb_instruction_table[0x30] = SWAP_r;
    cb_instruction_table[0x31] = SWAP_r;
    cb_instruction_table[0x32] = SWAP_r;
    cb_instruction_table[0x33] = SWAP_r;
    cb_instruction_table[0x34] = SWAP_r;
    cb_instruction_table[0x35] = SWAP_r;
    cb_instruction_table[0x36] = SWAP_r;
    cb_instruction_table[0x37] = SWAP_r;

    cb_instruction_table[0x38] = SRL_r;
    cb_instruction_table[0x39] = SRL_r;
    cb_instruction_table[0x3A] = SRL_r;
    cb_instruction_table[0x3B] = SRL_r;
    cb_instruction_table[0x3C] = SRL_r;
    cb_instruction_table[0x3D] = SRL_r;
    cb_instruction_table[0x3E] = SRL_r;
    cb_instruction_table[0x3F] = SRL_r;

    for(size_t i = 0x40; i < 0x80; i++) { cb_instruction_table[i] = BIT_n_r; }
    for(size_t i = 0x80; i < 0xC0; i++) { cb_instruction_table[i] = RES_n_r; }
    for(size_t i = 0xC0; i <= 0xFF; i++){ cb_instruction_table[i] = SET_n_r; }

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
    memory[0xFF05] = 0x00; memory[0xFF06] = 0x00; memory[0xFF07] = 0x00;
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
    memory[0xFFFF] = 0x00;
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
    switch (event->key.keysym.sym) {
        case SDLK_b:    joypad.start  = is_pressed; break;
        case SDLK_v:    joypad.select = is_pressed; break;
        case SDLK_m:    joypad.b      = is_pressed; break;
        case SDLK_k:    joypad.a      = is_pressed; break;
        case SDLK_s:    joypad.down   = is_pressed; break;
        case SDLK_w:    joypad.up     = is_pressed; break;
        case SDLK_a:    joypad.left   = is_pressed; break;
        case SDLK_d:    joypad.right  = is_pressed; break;
    }
}

int main(int argc, char **argv){
    if(argc <= 1){
        fprintf(stderr, "[ERROR] Usage: ./gameboy <path-to-ROM>\n");
        return 1;
    }


    CPU cpu = {0};
    PPU ppu = {0};
    TIMER timer = {0};
    InitializeInstructionTable();
    InitializePowerOnState(&cpu, &ppu);
    InitializeBootROM();
    InitializeGameROM(argv[1]);
    //create_dummy_header();

    print_cpu_state(&cpu);

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

    while(cpu.running){
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
            
            if (cpu.halted) {
                cycles_executed += 4;
            } else {
                uint8_t opcode = FetchByte(&cpu);        
                cycles_executed = instruction_table[opcode](&cpu);
            }

            cycles_this_frame += cycles_executed;

            ppu_step(&ppu, cycles_executed);
            timer_step(&timer, cycles_executed);
        }
        
        SDL_UpdateTexture(texture, NULL, framebuffer, USER_WINDOW_WIDTH * sizeof(uint32_t));  // Update the texture with the new pixel data
        SDL_RenderClear(renderer); // Clear the renderer
        SDL_RenderCopy(renderer, texture, NULL, NULL); // Copy the texture to the renderer
        SDL_RenderPresent(renderer); // Present the renderer

        clock_gettime(CLOCK_MONOTONIC, &end_time);
        long time_elapsed_ns = (end_time.tv_sec - start_time.tv_sec) * 1000000000L +
                               (end_time.tv_nsec - start_time.tv_nsec);

        sleep_duration_ns = NANOSECONDS_PER_FRAME - time_elapsed_ns;

        if (sleep_duration_ns > 0) {
            //printf("sleeped\n");
            struct timespec sleep_spec = {0, sleep_duration_ns};
            nanosleep(&sleep_spec, NULL);
        }
    }

    // --- Cleanup ---
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}