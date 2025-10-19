#include "serial.h"
#include "device.h"

extern DEVICE device;

void serial_step(int cycles){
    // by now assuming an interrupt each 8 bytes transferred but
    // always with disconnected cable so SB reads always 0xFF
    // TODO implement real ipc with sockets
    device.serial_port.cycles_elapsed += cycles;
    
    // one byte has been trasferred so raise an interrupt
    if(device.serial_port.cycles_elapsed / SERIAL_CLOCK >= 8){
        device.serial_port.cycles_elapsed -= 8 * SERIAL_CLOCK;
        device.memory[IF_REG] |= 0x08;
    }
}

uint8_t ReadFromSerialPort(){
    return 0xFF;
}

