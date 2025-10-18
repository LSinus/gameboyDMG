#include <string.h>

#include "memory.h"
#include "ppu.h"
#include "timer.h"
#include "joypad.h"
#include "device.h"
#include "cartridge.h"

extern DEVICE device;

void WriteMem(uint16_t addr, uint8_t data){

    /* If address belongs to the cartridge call 
     * WriteToCartridge in order to manage coorectly
     * the MBC if present
     */
    if(addr <= 0x7FFF) {
        WriteToCartridge(addr, data);
        return;
    } 
    
    uint8_t ppu_mode = ppu_get_mode();

    //Check for VRAM read restrictions
    uint8_t LCDC = device.memory[0xFF40];
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

    /* When the lcd and ppu are disabled resets ppu state 
       The LCD & PPU enable is the last bit of the data, 0
       means disabled
    */
    if(addr == 0xFF40 && (data & 0x80) == 0){
        // LY reset to 0
        device.memory[0xFF44] = 0;
        device.ppu.ly = 0;
        ppu_set_mode(MODE_0_HBLANK);
    }


    if(addr == 0xFF44) return; // LY is not writable

    if(addr == 0xFF50){
        device.boot_rom_enabled = false; // Disable the boot ROM
    }

    if(addr == 0xFF46){ // DMA transfer
        uint16_t transfer_source = data * 0x0100;
        memcpy(&(device.memory[0xFE00]), &(device.memory[transfer_source]), 40*4); // 40 sprites 4 byte each
        device.dma.running = true;
        device.dma.cycles = 0;
    }

    if(addr != DIV_REG) device.memory[addr] = data;
    else { // writing DIV register resets it
        device.memory[DIV_REG] = 0x00; 
        device.timer.div_cycle_counter = 0;
        device.timer.tima_cycle_counter = 0;
    }
}

uint8_t ReadMem(uint16_t addr){
    uint8_t ppu_mode = ppu_get_mode();

    // check for dma running
    if(device.dma.running){
        if(addr < 0xFF80 || addr > 0xFFFE){
            return 0xFF;
        }
    }

    // Check for VRAM read restrictions
    //Check for VRAM read restrictions
    uint8_t LCDC = device.memory[0xFF40];
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

    if(device.boot_rom_enabled && addr < 0x0100){
        return device.boot[addr];
    }

    if(addr == 0xFF00){
        uint8_t P1 = device.memory[0xFF00];
        P1 |= 0x0F; // all buttons unpressed (0 pressed 1 unpressed)
        if((P1 & 0x10) == 0){ // D-Pad buttons
            if (device.joypad.right) P1 &= ~0x01; // Bit 0 (Right)
            if (device.joypad.left)  P1 &= ~0x02; // Bit 1 (Left)
            if (device.joypad.up)    P1 &= ~0x04; // Bit 2 (Up)
            if (device.joypad.down)  P1 &= ~0x08; // Bit 3 (Down)
        }
        if ((P1 & 0x20) == 0) { // Action buttons
            if (device.joypad.a)      P1 &= ~0x01; // Bit 0 (A)
            if (device.joypad.b)      P1 &= ~0x02; // Bit 1 (B)
            if (device.joypad.select) P1 &= ~0x04; // Bit 2 (Select)
            if (device.joypad.start)  P1 &= ~0x08; // Bit 3 (Start)
        }
        return P1;
    }

    // MBC logic, cartridge manages the reads from there
    if(addr <= 0x7FFF){
        return ReadFromCartridge(addr);
    }

    return device.memory[addr];
}

/* This function fetches and returns a byte from memory at the address of
   the program counter and increments it. */
uint8_t FetchByte(){
    if(device.cpu.halt_bug){
        device.cpu.halt_bug = false;
        return ReadMem(device.cpu.PC);
    }
    return ReadMem(device.cpu.PC++);
}

/* This function fetches and return a 16-bit word from memory at the address of
   the program counter and increments it. The word is stored in little endian
   so the shift is needed to return the right value */
uint16_t FetchWord(){
    uint16_t lsb = (uint16_t)FetchByte();
    uint16_t msb = (uint16_t)FetchByte();
    return (msb << 8) | lsb;
}



/* This function updates the dma if active */
void dma_step(int cycles){
    if(device.dma.running){
        device.dma.cycles += cycles;
        if(device.dma.cycles >= 640) device.dma.running = false;
    }
}
