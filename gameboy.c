/* Gameboy emulator by Leonardo Sinibaldi
   Started 19th July 2025.
*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>

#define CLOCK_FREQ_HZ 4194304
#define FRAME_RATE_HZ 59.7
#define CYCLES_PER_FRAME (CLOCK_FREQ_HZ / FRAME_RATE_HZ)
#define NANOSECONDS_PER_FRAME (1000000000L / FRAME_RATE_HZ)

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

/* This function fetches and return a byte from memory at the address of
   the program counter and increments it. */
uint8_t FetchByte(CPU *cpu){
    return memory[cpu->PC++];
}

/* This function fetches and return a 16-bit word from memory at the address of
   the program counter and increments it. The word is stored in little endian
   so the shift is needed to return the right value */
uint16_t FetchWord(CPU *cpu){
    uint16_t lsb = (uint16_t)FetchByte(cpu);
    uint16_t msb = (uint16_t)FetchByte(cpu);
    return (msb << 8) | lsb;
}

void WriteMem(uint16_t addr, uint8_t data){
    if(addr != 0xFF04) memory[addr] = data;
    else memory[addr] = 0x00; // writing DIV register resets it
}


/* Instruction function pointer type */
typedef int (*Instruction)(CPU *cpu);

/* Look-up table of function pointers for 8-bit instructions */
Instruction instruction_table[256];

/* Look-up table of function pointers for CB-prefixed instructions */
Instruction cb_instruction_table[256];


/* ---- FUNCTION POINTERS FOR OPCODES SECTION ---- */
int UNKNOWN(CPU *cpu){
    uint8_t opcode = memory[cpu->PC - 1];
    fprintf(stderr, "Error: Unknown opcode 0x%02X at address 0x%04X\n", opcode, cpu->PC - 1);
    // Stop the emulator
    cpu->running = 0; 
    return 0;
}

/* ---  GMB 8bit-Loadcommands --- */

/* This performs a store of a word contained in A register to memory 
   at the address contained in BC */
int LD_BCmem_A(CPU *cpu) {
    WriteMem(cpu->BC, (uint8_t) (cpu->AF & 0xFF00) >> 8);
    return 8;
}

/* This performs a store of a word contained in A register to memory 
   at the address contained in DE */
int LD_DEmem_A(CPU *cpu) {
    WriteMem(cpu->DE,(uint8_t) (cpu->AF & 0xFF00) >> 8);
    return 8;
}

/* This performs a store of a word contained in A register to memory 
   at the address contained in a 16-bit immediate value */
int LD_d16mem_A(CPU *cpu) {
    uint16_t addr = FetchWord(cpu);
    WriteMem(addr,(uint8_t) (cpu->AF & 0xFF00) >> 8);
    return 16;
}


/* This performs a store of a word contained in A register to memory 
   at the address contained in HL and increments it */
int LDI_HLmem_A(CPU * cpu){
    WriteMem(cpu->HL, (uint8_t) (cpu->AF & 0xFF00) >> 8);
    cpu->HL++;
    return 8;
}

/* This performs a store of a word contained in A register to memory 
   at the address contained in HL and decrements it */
int LDD_HLmem_A(CPU * cpu){
    WriteMem(cpu->HL, (uint8_t) (cpu->AF & 0xFF00) >> 8);
    cpu->HL--;
    return 8;
}

/* This performs a load into A of a word contained in memory 
   at the address stored in HL and increments it */
int LDI_A_HLmem(CPU * cpu){
    uint16_t value = (uint16_t)memory[cpu->HL++];
    cpu->AF = (value << 8) | (cpu->AF & 0x00FF);
    return 8;
}

/* This performs a load into A of a word contained in memory 
   at the address stored in HL and decrements it */
int LDD_A_HLmem(CPU * cpu){
    uint16_t value = (uint16_t)memory[cpu->HL--];
    cpu->AF = (value << 8) | (cpu->AF & 0x00FF);
    return 8;
}

/* This performs a load into A of a word contained in memory 
   at the address stored in BC register */
int LD_A_BCmem(CPU * cpu){
    uint16_t value = (uint16_t)memory[cpu->BC];
    cpu->AF = (value << 8) | (cpu->AF & 0x00FF);
    return 8;
}

/* This performs a load into A of a word contained in memory 
   at the address stored in DE register */
int LD_A_DEmem(CPU * cpu){
    uint16_t value = (uint16_t)memory[cpu->DE];
    cpu->AF = (value << 8) | (cpu->AF & 0x00FF);
    return 8;
}

/* This performs a load into A of a word contained in memory 
   at the address stored in an immediate 16 bit value */
int LD_A_d16mem(CPU * cpu){
    uint16_t addr = FetchWord(cpu);
    uint16_t value = (uint16_t)memory[addr];
    cpu->AF = (value << 8) | (cpu->AF & 0x00FF);
    return 16;
}

/* Load data from a register r into another register r (opcodes 0x40-0x7F, except 0x76 that is HALT instruction) */
int LD_r_r(CPU *cpu) {
    uint8_t opcode = memory[cpu->PC - 1];

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
        case 6: n = memory[cpu->HL];           break; // Value from address (HL)
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

/* Load an immediate 8-bit value into a register r */
int LD_r_d8(CPU *cpu) {
    uint8_t opcode = memory[cpu->PC - 1];

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
    cpu->AF = (cpu->AF & 0x00FF) | ((uint16_t)(memory[0xFF00 + n]) << 8);
    return 12;
}

/* This reads from IO-port in register C into A register */
int LD_A_Cmem(CPU *cpu){
    uint8_t c = (uint8_t)(cpu->BC & 0x00FF);
    cpu->AF = (cpu->AF & 0x00FF) | ((uint16_t)(memory[0xFF00 + c]) << 8);
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
    uint8_t opcode = memory[cpu->PC-1];

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0x80: n = cpu->BC >> 8;     break; // Register B
        case 0x81: n = cpu->BC & 0xFF;   break; // Register C
        case 0x82: n = cpu->DE >> 8;     break; // Register D
        case 0x83: n = cpu->DE & 0xFF;   break; // Register E
        case 0x84: n = cpu->HL >> 8;     break; // Register H
        case 0x85: n = cpu->HL & 0xFF;   break; // Register L
        case 0x86: n = memory[cpu->HL];  break; // Address (HL)
        case 0x87: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = n + a;

    cpu->AF &= 0xFF00; // Flags reset
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return opcode == 0x86 ? 8 : 4;
}

/* Adds an immediate 8-bit value to the accumulator and stores the result there */
int ADD_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    
    uint16_t result = n + a;

    cpu->AF &= 0xFF00; // Flags reset
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return 8;
}

/* Adds value stored in r register and carry to the accumulator and stores the result there */
int ADC_A_r(CPU *cpu){
    uint8_t opcode = memory[cpu->PC-1];

    uint16_t n;
    uint16_t a = cpu->AF >> 8;
    uint16_t c = cpu->AF & 0x0010;
    

    switch (opcode){
        case 0x88: n = cpu->BC >> 8;     break; // Register B
        case 0x89: n = cpu->BC & 0xFF;   break; // Register C
        case 0x8A: n = cpu->DE >> 8;     break; // Register D
        case 0x8B: n = cpu->DE & 0xFF;   break; // Register E
        case 0x8C: n = cpu->HL >> 8;     break; // Register H
        case 0x8D: n = cpu->HL & 0xFF;   break; // Register L
        case 0x8E: n = memory[cpu->HL];  break; // Address (HL)
        case 0x8F: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = n + a + c;

    cpu->AF &= 0xFF00; // Flags reset
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) + (c & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return opcode == 0x8E ? 8 : 4;
}

/* Adds an immediate 8-bit value and carry to the accumulator and stores the result there */
int ADC_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    uint16_t c = cpu->AF & 0x0010;
    
    uint16_t result = n + a + c;

    cpu->AF &= 0xFF00; // Flags reset
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) + (c & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return 8;
}

/* Subtracts value stored in r register to the accumulator and stores the result there */
int SUB_A_r(CPU *cpu){
    uint8_t opcode = memory[cpu->PC-1];

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0x90: n = cpu->BC >> 8;     break; // Register B
        case 0x91: n = cpu->BC & 0xFF;   break; // Register C
        case 0x92: n = cpu->DE >> 8;     break; // Register D
        case 0x93: n = cpu->DE & 0xFF;   break; // Register E
        case 0x94: n = cpu->HL >> 8;     break; // Register H
        case 0x95: n = cpu->HL & 0xFF;   break; // Register L
        case 0x96: n = memory[cpu->HL];  break; // Address (HL)
        case 0x97: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a - n;

    cpu->AF &= 0xFF40; // Flags reset and N = 1
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F)) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(a < n) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return opcode == 0x96 ? 8 : 4;
}

/* Subtracts an immediate 8-bit value to the accumulator and stores the result there */
int SUB_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    
    uint16_t result = a - n;

    cpu->AF &= 0xFF40; // Flags reset and N = 1
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F)) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(a < n) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return 8;
}

/* Subtracts value stored in r register and carry to the accumulator and stores the result there */
int SBC_A_r(CPU *cpu){
    uint8_t opcode = memory[cpu->PC-1];

    uint16_t n;
    uint16_t a = cpu->AF >> 8;
    uint16_t c = cpu->AF & 0x0010;

    switch (opcode){
        case 0x98: n = cpu->BC >> 8;     break; // Register B
        case 0x99: n = cpu->BC & 0xFF;   break; // Register C
        case 0x9A: n = cpu->DE >> 8;     break; // Register D
        case 0x9B: n = cpu->DE & 0xFF;   break; // Register E
        case 0x9C: n = cpu->HL >> 8;     break; // Register H
        case 0x9D: n = cpu->HL & 0xFF;   break; // Register L
        case 0x9E: n = memory[cpu->HL];  break; // Address (HL)
        case 0x9F: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a - n - c;

    cpu->AF &= 0xFF40; // Flags reset and N = 1
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F) + c) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(a < n + c) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return opcode == 0x9E ? 8 : 4;
}

/* Subtracts an immediate 8-bit value and carry to the accumulator and stores the result there */
int SBC_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    uint16_t c = cpu->AF & 0x0010;
    
    uint16_t result = a - n - c;

    cpu->AF &= 0xFF40; // Flags reset and N = 1
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F) + c) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;

    if(a < n + c) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return 8;
}

/* Does the logical AND between r register and the accumulator and stores the result there */
int AND_A_r(CPU *cpu){
    uint8_t opcode = memory[cpu->PC-1];

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0xA0: n = cpu->BC >> 8;     break; // Register B
        case 0xA1: n = cpu->BC & 0xFF;   break; // Register C
        case 0xA2: n = cpu->DE >> 8;     break; // Register D
        case 0xA3: n = cpu->DE & 0xFF;   break; // Register E
        case 0xA4: n = cpu->HL >> 8;     break; // Register H
        case 0xA5: n = cpu->HL & 0xFF;   break; // Register L
        case 0xA6: n = memory[cpu->HL];  break; // Address (HL)
        case 0xA7: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a & n;

    cpu->AF &= 0xFF20; // Flags reset and H = 1
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return opcode == 0xA6 ? 8 : 4;
}
/* Does the logical AND between an immediate 8-bit value and the accumulator and stores the result there */
int AND_A_d8(CPU *cpu){
    uint16_t n = FetchByte(cpu);
    uint16_t a = cpu->AF >> 8;
    
    uint16_t result = a & n;

    cpu->AF &= 0xFF20; // Flags reset and H = 1
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return 8;
}

/* Does the logical OR between r register and the accumulator and stores the result there */
int OR_A_r(CPU *cpu){
    uint8_t opcode = memory[cpu->PC-1];

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0xB0: n = cpu->BC >> 8;     break; // Register B
        case 0xB1: n = cpu->BC & 0xFF;   break; // Register C
        case 0xB2: n = cpu->DE >> 8;     break; // Register D
        case 0xB3: n = cpu->DE & 0xFF;   break; // Register E
        case 0xB4: n = cpu->HL >> 8;     break; // Register H
        case 0xB5: n = cpu->HL & 0xFF;   break; // Register L
        case 0xB6: n = memory[cpu->HL];  break; // Address (HL)
        case 0xB7: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a | n;

    cpu->AF &= 0xFF00; // Flags reset
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

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

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return 8;
}

/* Does the logical XOR between r register and the accumulator and stores the result there */
int XOR_A_r(CPU *cpu){
    uint8_t opcode = memory[cpu->PC-1];

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0xA8: n = cpu->BC >> 8;     break; // Register B
        case 0xA9: n = cpu->BC & 0xFF;   break; // Register C
        case 0xAA: n = cpu->DE >> 8;     break; // Register D
        case 0xAB: n = cpu->DE & 0xFF;   break; // Register E
        case 0xAC: n = cpu->HL >> 8;     break; // Register H
        case 0xAD: n = cpu->HL & 0xFF;   break; // Register L
        case 0xAE: n = memory[cpu->HL];  break; // Address (HL)
        case 0xAF: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a ^ n;

    cpu->AF &= 0xFF00; // Flags reset
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

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

    cpu->AF = (cpu->AF & 0x00FF) | (result << 8);

    return 8;
}

/* Compares value stored in r register to the accumulator */
int CP_A_r(CPU *cpu){
    uint8_t opcode = memory[cpu->PC-1];

    uint16_t n;
    uint16_t a = cpu->AF >> 8;

    switch (opcode){
        case 0xB8: n = cpu->BC >> 8;     break; // Register B
        case 0xB9: n = cpu->BC & 0xFF;   break; // Register C
        case 0xBA: n = cpu->DE >> 8;     break; // Register D
        case 0xBB: n = cpu->DE & 0xFF;   break; // Register E
        case 0xBC: n = cpu->HL >> 8;     break; // Register H
        case 0xBD: n = cpu->HL & 0xFF;   break; // Register L
        case 0xBE: n = memory[cpu->HL];  break; // Address (HL)
        case 0xBF: n = cpu->AF >> 8;     break; // Register A
    }
    
    uint16_t result = a - n;

    cpu->AF &= 0xFF40; // Flags reset and N = 1
    if(result == 0) cpu->AF |= 0x80; // Zero flag
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
    
    uint16_t result = n - a;

    cpu->AF &= 0xFF40; // Flags reset and N = 1
    if(result == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    if((a & 0x0F) < (n & 0x0F)) cpu->AF |= 0x20; // Half-carry flag
    else cpu->AF &= ~0x20;  

    if(a < n) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    return 8;
}

/* This increments the value in the 8bit register */
int INC_r(CPU *cpu) {
    uint8_t opcode = memory[cpu->PC - 1];
    uint8_t dest_id = (opcode >> 3) & 0x07; 

    uint8_t value;

    switch (dest_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint8_t opcode = memory[cpu->PC - 1];
    uint8_t dest_id = (opcode >> 3) & 0x07; 

    uint8_t value;

    switch (dest_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint16_t correction = 0;
    
    bool n_flag = (cpu->AF & 0x40) != 0;
    bool h_flag = (cpu->AF & 0x20) != 0;
    bool c_flag = (cpu->AF & 0x10) != 0;

    if (n_flag) { // Last operation was a subtraction
        if (h_flag) {
            correction -= 0x06;
        }
        if (c_flag) {
            correction -= 0x60;
        }
    } else { // Last operation was an addition
        if (h_flag || (a & 0x0F) > 9) {
            correction += 0x06;
        }
        if (c_flag || a > 0x99) {
            correction += 0x60;
            cpu->AF |= 0x10; // The new Carry flag gets set if a wrap-around happened
        }
    }

    a += correction;

    if (a == 0) cpu->AF |= 0x80; // Zero flag
    else cpu->AF &= ~0x80;

    cpu->AF &= ~0x20; // Clear H flag
    
    cpu->AF = (uint16_t)(a << 8) | (cpu->AF & 0x00F0);

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

    cpu->AF &= ~0x80; // Clear the Z flag
    cpu->AF &= ~0x40; // Clear the N flag

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    if((cpu->SP & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half carry flag
    else cpu->AF &= ~0x20;

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
    cpu->BC = (uint16_t)memory[cpu->SP++];
    cpu->BC = ((cpu->BC & 0x00FF) | (uint16_t)memory[cpu->SP++] << 8);
    return 12;
}

/* Pop from the stack a value and stores it in DE register */
int POP_DE(CPU *cpu){
    cpu->DE = (uint16_t)memory[cpu->SP++];
    cpu->DE = ((cpu->DE & 0x00FF) | (uint16_t)memory[cpu->SP++] << 8);
    return 12;
}
/* Pop from the stack a value and stores it in HL register */
int POP_HL(CPU *cpu){
    cpu->HL = (uint16_t)memory[cpu->SP++];
    cpu->HL = ((cpu->HL & 0x00FF) | (uint16_t)memory[cpu->SP++] << 8);
    return 12;
}
/* Pop from the stack a value and stores it in AF register */
int POP_AF(CPU *cpu){
    cpu->AF = (uint16_t)memory[cpu->SP++] & 0x00F0;
    cpu->AF = ((cpu->AF & 0x00FF) | (uint16_t)memory[cpu->SP++] << 8);
    return 12;
}
/* ---------------------------------------- */

/* --- GMB 16-bit arithmetic and logic --- */

/* Increments a 16-bit register */
int INC_rr(CPU *cpu){
    uint8_t opcode = memory[cpu->PC-1];

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
    uint8_t opcode = memory[cpu->PC-1];

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
    uint8_t opcode = memory[cpu->PC-1];
    uint16_t n;
    switch(opcode){
        case 0x09: n = cpu->BC; break;
        case 0x19: n = cpu->DE; break;
        case 0x29: n = cpu->HL; break;
        case 0x39: n = cpu->SP; break; 
    }
    uint16_t result = cpu->HL + n;

    cpu->AF &= ~0x40; // Clear the N flag

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    if((cpu->HL & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half carry flag
    else cpu->AF &= ~0x20;

    cpu->HL = result;
    return 8;
}

/* Adds an immediate signed 8-bit value to SP */
int ADD_SP_s8(CPU *cpu){
    int8_t n = (int8_t)FetchByte(cpu);

    uint16_t result = cpu->SP + n;

    cpu->AF &= ~0x80; // Clear the Z flag
    cpu->AF &= ~0x40; // Clear the N flag

    if(result > 0xFF) cpu->AF |= 0x10; // Carry flag
    else cpu->AF &= ~0x10;

    if((cpu->HL & 0x0F) + (n & 0x0F) > 0x0F) cpu->AF |= 0x20; // Half carry flag
    else cpu->AF &= ~0x20;

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
    if((cpu->AF & 0x0080) == 0x0000){ // Zero flag not set
        return JP_d16(cpu);
    }
    return 12;
}

/* Jumps to an address represented by an immediate 16-bit value
   if and only if the carry flag is not set */
int JP_NC_d16(CPU *cpu){
    if((cpu->AF & 0x0010) == 0x0000){ // Carry flag not set
        return JP_d16(cpu);
    }
    return 12;
}

/* Jumps to an address represented by an immediate 16-bit value
   if and only if the zero flag is set */
int JP_Z_d16(CPU *cpu){
    if((cpu->AF & 0x0080) == 0x0080){ // Zero flag set
        return JP_d16(cpu);
    }
    return 12;
}

/* Jumps to an address represented by an immediate 16-bit value
   if and only if the carry flag is set */
int JP_C_d16(CPU *cpu){
    if((cpu->AF & 0x0010) == 0x0010){ // Carry flag set
       return JP_d16(cpu);
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
    if((cpu->AF & 0x0080) == 0x0000){ // Zero flag not set
        return JR_d8(cpu);
    }
    return 8;
}

/* Relatively jumps with an offset represented by an immediate 8-bit value
   if and only if the carry flag is not set */
int JR_NC_d8(CPU *cpu){
    if((cpu->AF & 0x0010) == 0x0000){ // Carry flag not set
       return JR_d8(cpu);
    }
    return 8;
}

/* Relatively jumps with an offset represented by an immediate 8-bit value
   if and only if the zero flag is set */
int JR_Z_d8(CPU *cpu){
    if((cpu->AF & 0x0080) == 0x0080){ // Zero flag set
        return JR_d8(cpu);
    }
    return 8;
}

/* Relatively jumps with an offset represented by an immediate 8-bit value
   if and only if the carry flag is set */
int JR_C_d8(CPU *cpu){
    if((cpu->AF & 0x0010) == 0x0010){ // Carry flag set
        return JR_d8(cpu);
    }
    return 8;
}

/* Calls a procedure saving program counter */
int CALL(CPU *cpu){
    uint16_t address = FetchWord(cpu);
    cpu->SP -= 2;
    WriteMem(cpu->SP, cpu->PC);
    cpu->PC = address;
    return 24; 
}

/* Calls a procedure saving program counter 
   if and only if zero flag is not set
 */
int CALL_NZ(CPU *cpu){
    if((cpu->AF & 0x0080) == 0x0000){ // Zero flag not set
        return CALL(cpu);
    }
    return 12;
}

/* Calls a procedure saving program counter 
   if and only if zero flag is set
 */
int CALL_Z(CPU *cpu){
    if((cpu->AF & 0x0080) == 0x0080){ // Zero flag set
        return CALL(cpu);
    }
    return 12;
}

/* Calls a procedure saving program counter 
   if and only if carry flag is not set
 */
int CALL_NC(CPU *cpu){
    if((cpu->AF & 0x0010) == 0x0000){ // Carry flag not set
       return CALL(cpu);
    }
    return 12;
}

/* Calls a procedure saving program counter 
   if and only if carry flag is set
 */
int CALL_C(CPU *cpu){
    if((cpu->AF & 0x0010) == 0x0010){ // Carry flag set
        return CALL(cpu);
    }
    return 12;
}

/* Returns restoring previous program counter */
int RET(CPU *cpu){
    cpu->PC = memory[cpu->SP];
    cpu->SP += 2;
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
    uint8_t opcode = memory[cpu->PC -1];
    uint16_t address;

    switch(opcode){
        case 0xC7: address = 0x00;
        case 0xCF: address = 0x08;
        case 0xD7: address = 0x10;
        case 0xDF: address = 0x18;
        case 0xE7: address = 0x20;
        case 0xEF: address = 0x28;
        case 0xF7: address = 0x30;
        case 0xFF: address = 0x38;
    }

    cpu->SP -= 2;
    WriteMem(cpu->SP, cpu->PC);
    cpu->PC = address;
    return 16; 
}

/* --- GMB CPU-Controlcommands --- */

/* This performs a no-operation on the cpu*/
int NOP(CPU *cpu){ return 4; }

/* This performs the HALT instruction */
int HALT(CPU *cpu){ 
    // TODO HANDLE INTERRUPTS
    cpu->halted = true;
    return 4;    
}

int STOP(CPU *cpu){
    /* The STOP instruction is two bytes long. The 0x10 has already been
       fetched, so fetch the following 0x00 byte. */
    FetchByte(cpu);

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
        new_a |= 0x0010;  // add the carry as most sign. bit
    }

    cpu->AF = (new_a << 8) | (cpu->AF & 0x00FF); 
    return 4;
}

/* Rotates left the value stored in register r */
int RLC_r(CPU *cpu){
    uint8_t opcode = memory[cpu->PC-1];

    uint8_t value;

    switch (opcode) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = memory[cpu->HL];           break; // (HL)
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

    switch (opcode) {
        case 0: cpu->BC = ((uint16_t)(value) << 8) | (cpu->BC & 0x00FF);   break; // B
        case 1: cpu->BC = (cpu->BC & 0xFF00) | value;                      break; // C
        case 2: cpu->DE = ((uint16_t)(value) << 8) | (cpu->DE & 0x00FF);   break; // D
        case 3: cpu->DE = (cpu->DE & 0xFF00) | value;                      break; // E
        case 4: cpu->HL = ((uint16_t)(value) << 8) | (cpu->HL & 0x00FF);   break; // H
        case 5: cpu->HL = (cpu->HL & 0xFF00) | value;                      break; // L
        case 6: WriteMem(cpu->HL, value);                                  break; // (HL)
        case 7: cpu->AF = ((uint16_t)(value) << 8) | (cpu->AF & 0x00FF);   break; // A
    }

    return opcode == 6 ? 16 : 8;
}

/* Rotates right the value stored in register r */
int RRC_r(CPU *cpu){
    uint8_t opcode = memory[cpu->PC-1];
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint8_t opcode = memory[cpu->PC-1];
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint8_t opcode = memory[cpu->PC-1];
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint8_t opcode = memory[cpu->PC-1];
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint8_t opcode = memory[cpu->PC-1];
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint8_t opcode = memory[cpu->PC-1];
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint8_t opcode = memory[cpu->PC-1];
    uint8_t source_id = opcode & 0x07;

    uint8_t value;

    switch (source_id) {
        case 0: value = (uint8_t)(cpu->BC >> 8);   break; // B
        case 1: value = (uint8_t)(cpu->BC & 0xFF); break; // C
        case 2: value = (uint8_t)(cpu->DE >> 8);   break; // D
        case 3: value = (uint8_t)(cpu->DE & 0xFF); break; // E
        case 4: value = (uint8_t)(cpu->HL >> 8);   break; // H
        case 5: value = (uint8_t)(cpu->HL & 0xFF); break; // L
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint8_t opcode = memory[cpu->PC-1];

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
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint8_t opcode = memory[cpu->PC-1];

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
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    uint8_t opcode = memory[cpu->PC-1];

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
        case 6: value = memory[cpu->HL];           break; // (HL)
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
    instruction_table[0xD2] = JP_NZ_d16;
    instruction_table[0xCA] = JP_Z_d16;
    instruction_table[0xDA] = JP_C_d16;

    instruction_table[0x18] = JR_d8;
    instruction_table[0x28] = JR_Z_d8;
    instruction_table[0x38] = JR_C_d8;
    instruction_table[0x20] = JR_NZ_d8;
    instruction_table[0x30] = JR_NC_d8;

    instruction_table[0xCD] = CALL;

    instruction_table[0xCC] = CALL_NZ;
    instruction_table[0xC4] = CALL_Z;
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

void InitializeCpu(CPU *cpu){
    cpu->SP = 0x1000;
    cpu->halted = false;
    cpu->running = true;
    cpu->IME = true;
}


void InitializeTestProgram() {
    FILE *program = fopen("Pokemon Red-Blue.gb", "rb");
    size_t program_length;
    if(program){
        fseek(program, 0, SEEK_END);
        program_length = ftell(program);
        fseek(program, 0, SEEK_SET);
        fread(memory, program_length, 1, program);
        fclose(program);
    }
}

int main(){
    CPU cpu = {0};
    InitializeInstructionTable();
    InitializeCpu(&cpu);
    InitializeTestProgram();

    struct timespec start_time, end_time;
    long sleep_duration_ns;

    while(cpu.running){
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        int cycles_this_frame = 0;
        while (cycles_this_frame < CYCLES_PER_FRAME && cpu.running){
            if(cpu.halted){
                cycles_this_frame += 4;
            }
            else{
                /* Fetch and execute one instruction */
                uint8_t opcode = FetchByte(&cpu);
                printf("pc: 0x%04X opcode: 0x%02X\n",cpu.PC, opcode);

                /* Decode and execute the instruction */
                int cycles_executed = instruction_table[opcode](&cpu);

                cycles_this_frame += cycles_executed;

                // TODO: Update other components (PPU, Timers) with cycles_executed
            }
        }
        
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

    return 0;
}