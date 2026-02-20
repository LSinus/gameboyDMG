#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// REGISTER DEFINED ADDRESSES
#define SB_REG   0xFF01 // Serial trasfer register
#define SC_REG   0xFF02 // Serial control register
#define DIV_REG  0xFF04 // Divider register
#define TIMA_REG 0xFF05 // Timer counter register
#define TMA_REG  0xFF06 // Timer modulo register
#define TAC_REG  0xFF07 // Timer control register
#define IF_REG   0xFF0F // Interrupt flags register
#define IE_REG   0xFFFF // Interrupt enable register
#define LY_REG   0xFF44 // Y-line register

uint8_t ReadMem(uint16_t addr);
void WriteMem(uint16_t addr, uint8_t data);
uint8_t FetchByte();
uint16_t FetchWord();
void dma_step(int cycles);


/* DMA state struct */
typedef struct DMA {
    bool running;
    uint64_t cycles;
} DMA;

#endif
