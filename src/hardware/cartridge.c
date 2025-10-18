#include "cartridge.h"
#include "device.h"

#include <stdlib.h>

extern DEVICE device;

/* Cartridge ROM types */
const char* cartridge_types[256] = {
    [0x00] = "ROM ONLY",
    [0x01] = "MBC1",
    [0x02] = "MBC1+RAM",
    [0x03] = "MBC1+RAM+BATTERY",
    [0x05] = "MBC2",
    [0x06] = "MBC2+BATTERY",
    [0x08] = "ROM+RAM 9",
    [0x09] = "ROM+RAM+BATTERY 9",
    [0x0B] = "MMM01",
    [0x0C] = "MMM01+RAM",
    [0x0D] = "MMM01+RAM+BATTERY",
    [0x0F] = "MBC3+TIMER+BATTERY",
    [0x10] = "MBC3+TIMER+RAM+BATTERY 10",
    [0x11] = "MBC3",
    [0x12] = "MBC3+RAM 10",
    [0x13] = "MBC3+RAM+BATTERY 10",
    [0x19] = "MBC5",
    [0x1A] = "MBC5+RAM",
    [0x1B] = "MBC5+RAM+BATTERY",
    [0x1C] = "MBC5+RUMBLE",
    [0x1D] = "MBC5+RUMBLE+RAM",
    [0x1E] = "MBC5+RUMBLE+RAM+BATTERY",
    [0x20] = "MBC6",
    [0x22] = "MBC7+SENSOR+RUMBLE+RAM+BATTERY",
    [0xFC] = "POCKET CAMERA",
    [0xFD] = "BANDAI TAMA5",
    [0xFE] = "HuC3",
    [0xFF] = "HuC1+RAM+BATTERY"
};

const uint16_t cartridge_rom_sizes[8] = {
    [0x00] = 0,
    [0x01] = 2, // Unofficial
    [0x02] = 8,
    [0x03] = 32,
    [0x04] = 128,
    [0x05] = 64
};

bool InitializeCartridge(char *game_path){
    device.cartridge.gamePath = game_path;
    
    size_t program_length;
    FILE *program = fopen(game_path, "rb");

    if(!program){
        return false;
    }

    fseek(program, 0, SEEK_END);
    program_length = ftell(program);
    fseek(program, 0, SEEK_SET);
    device.cartridge.data = malloc(program_length);

    if(!device.cartridge.data){
        fclose(program);
        return false;
    }

    fread(device.cartridge.data, program_length, 1, program);
    fclose(program);

    device.cartridge.title = (char *)(&device.cartridge.data[0x0134]); 
    device.cartridge.type = cartridge_types[device.cartridge.data[0x0147]];
    device.cartridge.ROMsize = 32 * (1 << device.cartridge.data[0x148]);
    if(strstr(device.cartridge.type, "RAM") != NULL){
        device.cartridge.RAMsize = cartridge_rom_sizes[device.cartridge.data[0x149]];
    }
    else{
        device.cartridge.RAMsize = 0;
    }   
    
    return true;
}

void PrintCartridgeInfo(){
    CARTRIDGE *cart = &device.cartridge;

    if (cart->title) {
        printf("Cartridge type: %s\n", cart->type);
        printf("Cartridge title: %s\n", cart->title);
    } else {
        printf("Cartridge type: Unknown (0x%02X)\n", cart->data[0x0147]);
        return;
    }


    if(cart->ROMsize % 1000 != 0){
        printf("Rom size: %d MiB\n", cart->ROMsize % 1000);
    }
    else {
        printf("Rom size: %d KiB\n", cart->ROMsize);
    }
    if(cart->RAMsize % 1000 != 0){
        printf("Ram size: %d MiB\n", cart->RAMsize % 1000);
    }
    else {
        printf("Ram size: %d KiB\n", cart->RAMsize);
    }

}	

    
uint8_t ReadFromCartridge(uint16_t addr){
    if(addr >= 0x4000){
        //printf("read from bank %d, at base address: 0x%04X, offset: 0x%04X\n", device.cartridge.ROMnumber, device.cartridge.ROMnumber << 15, addr);
        return device.cartridge.data[(device.cartridge.ROMnumber * 0x4000) + (addr - 0x4000)];
    }
    return device.cartridge.data[addr];
}

void WriteToCartridge(uint16_t addr, uint8_t data){
    /* The addresses between 0x2000 and 0x3FFF are the selector
     * for ROM bank number if any, TODO by now only MBC1 is implemented.
     * Internally it is a 5-bit register with value from 0x01 to 0x1F (b---00000)
     * the area with zeros is the space of the 5-bit register.
     * It is possible to select bank from 0x01, if the 5 bits are all zeros the 01 is selected
     * as if the 5 bit are 00001.
     */
    if(addr >= 0x2000 && addr <= 0x3FFF){
        uint8_t number_of_banks = device.cartridge.ROMsize / 16; // Each ROM bank is 16 KiB //
        if(number_of_banks <= 2){
            // No external bank in cartrirdge
            return;
        }
        uint8_t mask;
        if(number_of_banks == 4){
            mask = 0b00000011;
        }
        else if(number_of_banks == 8){
            mask = 0b00000111;
        }
        else if(number_of_banks == 16){
            mask = 0b00001111;
        }
        else if(number_of_banks == 32){
            mask = 0b00011111;
        }
        // TODO implement larger MBC (>=1MiB)
        device.cartridge.ROMnumber = mask & data;
        if (device.cartridge.ROMnumber == 0) device.cartridge.ROMnumber = 1;
        //printf("Rom bank selected: %d\n", device.cartridge.ROMnumber);
    }



}


