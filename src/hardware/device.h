#ifndef DEVICE_H
#define DEVICE_H

#include <stdbool.h>

#include "cpu.h"
#include "joypad.h"
#include "memory.h"
#include "timer.h"
#include "cartridge.h"

typedef struct {
    CPU cpu;
    PPU ppu;
    JOYPAD joypad;
    TIMER timer;
    DMA dma;

    CARTRIDGE cartridge;
    char *romPath;

    bool boot_rom_enabled;
    uint8_t boot[256];
    uint8_t memory[65536];
    
} DEVICE;

#endif 