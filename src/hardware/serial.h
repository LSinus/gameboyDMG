#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#define SERIAL_CLOCK 8192

typedef struct SERIAL_PORT {
    size_t cycles_elapsed;
    bool transfer_in_progress;
} SERIAL_PORT;


/* This functions perform a step of cycles in 
 * the execution of a serial data transfer 
 */
void serial_step(int cycles);

/* This function reads from serial port in order to 
 * read from another device, by now it mimics the disconection
 * giving 0xff to all reads. TODO implement socket communication.
 */
uint8_t ReadFromSerialPort();

#endif
