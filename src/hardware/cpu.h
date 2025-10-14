#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

#include "ppu.h"

#define CLOCK_FREQ_HZ 4194304

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
    bool halt_bug;
    bool IME;
    bool request_IME;


#ifdef DEBUGGER_MODE
    bool slowed;
#endif
} CPU;

/* Instruction function pointer type */
typedef int (*Instruction)(CPU *cpu);

/* Look-up table of function pointers for 8-bit instructions */
extern Instruction instruction_table[256];

/* Look-up table of function pointers for CB-prefixed instructions */
extern Instruction cb_instruction_table[256];

void InitializeInstructionTable();
#endif
