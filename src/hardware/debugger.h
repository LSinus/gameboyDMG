#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "device.h"

static void sprint_hex(char* buffer, uint8_t val);
static int disassemble_instr(uint8_t data[3], char* buffer, size_t size);
int emulator_disassemble(uint16_t addr, char* buffer, size_t size);

#endif