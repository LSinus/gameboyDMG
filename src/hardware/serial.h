#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#define SERIAL_CLOCK 8192

typedef struct SERIAL_PORT {
    uint64_t cpu_cycles_elapsed;
    bool transfer_in_progress;
    bool master;
} SERIAL_PORT;

/* This functions perform a step of cycles in 
 * the execution of a serial data transfer 
 */
void serial_step(int cycles);

/* These functions handles the control register
 * of the serial port logic */
void WriteSerialControlReg(uint8_t data);
uint8_t ReadSerialControlReg();

/* This function reads from serial port in order to 
 * read from another device, by now it mimics the disconection
 * giving 0xff to all reads. TODO implement socket communication.
 */
uint8_t ReadFromSerialPort();

/* This function writes a byte into serial port */
void WriteToSerialPort(uint8_t data);


#endif
