#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "cpu.h"

// REGISTER DEFINED ADDRESSES
#define DIV_REG  0xFF04 // Divider register
#define TIMA_REG 0xFF05 // Timer counter register
#define TMA_REG  0xFF06 // Timer modulo register
#define TAC_REG  0xFF07 // Timer control register
#define IF_REG   0xFF0F // Interrupt flags register
#define IE_REG   0xFFFF // Interrupt enable register

extern bool boot_rom_enabled;
extern uint8_t boot[256];
extern uint8_t memory[65536];

uint8_t ReadMem(uint16_t addr);
void WriteMem(uint16_t addr, uint8_t data);
uint8_t FetchByte(CPU *cpu);
uint16_t FetchWord(CPU *cpu);
void dma_step(int cycles);


/* DMA state struct */
typedef struct DMA {
    bool running;
    size_t cycles;
} DMA;

extern DMA dma;

#endif
