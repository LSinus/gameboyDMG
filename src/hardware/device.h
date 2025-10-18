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

    bool boot_rom_enabled;
    uint8_t boot[256];
    uint8_t memory[65536];
    
} DEVICE;

void InitializePowerOnState();
void InitializeBootROM();
void create_dummy_header();
int handleInterrupts();
/* Copies the status of the emulator inside the buffer, the provided buffer 
   must be at least 82 bytes
*/
void GetEmulatorStatus(char* buf);
void GetEmulatorStatusFile(char* buf);

#endif 
