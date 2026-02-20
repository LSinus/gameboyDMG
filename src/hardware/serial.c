#include "serial.h"
#include "device.h"

extern DEVICE device;

void serial_step(int cycles){
    // by now assuming an interrupt each 8 bytes transferred but
    // always with disconnected cable so SB reads always 0xFF
    // TODO implement real ipc with sockets

    if(device.serial_port.transfer_in_progress) {
        if(device.serial_port.master) {
            device.serial_port.cpu_cycles_elapsed += cycles;

            // one byte has been trasferred so raise an interrupt
            if(device.serial_port.cpu_cycles_elapsed >= 4096){
                device.serial_port.cpu_cycles_elapsed -= 4096;

                //Understand better this thing
                device.memory[SB_REG] = 0xFF;

                device.serial_port.transfer_in_progress = false;
                device.memory[IF_REG] |= 0x08;
            }
        }
        else {
            // TODO implement external clock, this gameboy is slave
        }
    }
}


void WriteSerialControlReg(uint8_t data) {
    //hdevice.memory[SC_REGj] = data;
    if((data & 0x80) != 0) {
        //device.serial_port.transfer_in_progress = false;
        device.serial_port.transfer_in_progress = true;
        device.serial_port.cpu_cycles_elapsed = 0;
    }  
    else {
        device.serial_port.transfer_in_progress = false;
    }

    if((data & 0x01) != 0) {
        device.serial_port.master = true;
    }
    else {
        device.serial_port.master = false;
    }
}

uint8_t ReadSerialControlReg() {
    uint8_t data = 0;

    data |= (device.serial_port.master == true);
    data |= ((device.serial_port.transfer_in_progress == true) << 7);

    return data;
}

void WriteToSerialPort(uint8_t data)  {
    // TODO: Implement this logic
}


uint8_t ReadFromSerialPort(){
    return 0xFF;
}

