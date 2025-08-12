#include <string.h>

#include "memory.h"
#include "ppu.h"
#include "timer.h"
#include "joypad.h"

bool boot_rom_enabled = true;
uint8_t boot[256];
uint8_t memory[65536];

DMA dma = {0};

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

    if(addr == 0xFF50){
        boot_rom_enabled = false; // Disable the boot ROM
    }

    if(addr == 0xFF46){ // DMA transfer
        uint16_t transfer_source = data * 0x0100;
        memcpy(&memory[0xFE00], &memory[transfer_source], 40*4); // 40 sprites 4 byte each
        dma.running = true;
        dma.cycles = 0;
    }

    if(addr != DIV_REG) memory[addr] = data;
    else { // writing DIV register resets it
        memory[DIV_REG] = 0x00; 
        timer.div_cycle_counter = 0;
        timer.tima_cycle_counter = 0;
    }
}

uint8_t ReadMem(uint16_t addr){
    uint8_t ppu_mode = ppu_get_mode();

    // check for dma running
    if(dma.running){
        if(addr < 0xFF80 || addr > 0xFFFE){
            return 0xFF;
        }
    }

    #ifdef DEBUG_TEST_LOG
        if(addr == 0xFF44) return 0x90;
    #endif

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
    if(cpu->halt_bug){
        cpu->halt_bug = false;
        return ReadMem(cpu->PC);
    }
    return ReadMem(cpu->PC++);
}

/* This function fetches and return a 16-bit word from memory at the address of
   the program counter and increments it. The word is stored in little endian
   so the shift is needed to return the right value */
uint16_t FetchWord(CPU *cpu){
    uint16_t lsb = (uint16_t)FetchByte(cpu);
    uint16_t msb = (uint16_t)FetchByte(cpu);
    return (msb << 8) | lsb;
}



/* This function updates the dma if active */
void dma_step(int cycles){
    if(dma.running){
        dma.cycles += cycles;
        if(dma.cycles >= 640) dma.running = false;
    }
}
