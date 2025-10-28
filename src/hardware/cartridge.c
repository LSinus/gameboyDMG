#include "cartridge.h"
#include "device.h"

#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#ifndef WIN32
#include <unistd.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif  

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

/* This is a utility function used to calculate the
 * path of the save for the loaded game. It is on behalf of
 * the caller to ensure that the buffer has enough space
 * allocated
 */
static void get_save_path(char *save_path){
    char title_len = strlen(device.cartridge.title);
    memcpy(save_path, SAVES_PATH, sizeof(SAVES_PATH));
    memcpy(&save_path[sizeof(SAVES_PATH)-1], device.cartridge.title, title_len);
    save_path[sizeof(SAVES_PATH) - 1 + title_len] = '.';
    save_path[sizeof(SAVES_PATH) - 1 + title_len + 1] = 'b';
    save_path[sizeof(SAVES_PATH) - 1 + title_len + 2] = 'i';
    save_path[sizeof(SAVES_PATH) - 1 + title_len + 3] = 'n';
}

bool check_dir_exists_or_create(char *save_path){
    DIR* dir = opendir(SAVES_PATH);
    if (dir) {
        closedir(dir);
        return true;
    } else if (ENOENT == errno) {
        if(mkdir(SAVES_PATH, 0700) == -1){
            return false;
        }
        return true;
    } else {
        return false;
    }
}

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
        printf("[INFO] RAM size: %d\n", device.cartridge.RAMsize);

#ifndef WIN32
        if(strstr(device.cartridge.type, "BATTERY")){
            char save_path[100];
            get_save_path(save_path);

            printf("SAVE FILE PATH: %s\n", save_path);

            if(!check_dir_exists_or_create(SAVES_PATH)){
                perror("[ERROR] creating saves directory:");
                return false;
            }
            FILE *save = fopen(save_path, "a+b");

            if(!save){
                perror("[ERROR] opening save file");
                return false;
            }

            // THIS IMPLEMENTATION WORKS ONLY ON UNIX-LIKE SYSTEMS
            int save_fd = fileno(save);
            if(ftruncate(save_fd, device.cartridge.RAMsize * 0x0400) == -1){
                perror("[ERROR] resizing save file");
                return false;
            }
            device.cartridge.RAM = mmap(NULL, device.cartridge.RAMsize * 0x0400, PROT_READ | PROT_WRITE, MAP_SHARED, save_fd, 0);
            if(device.cartridge.RAM == MAP_FAILED){
                perror("[ERROR] mapping save file to memory");
                return false;
            }

            fclose(save);

            printf("[INFO] mapped ram with values: \n");
            for(int i = 0; i < device.cartridge.RAMsize * 0x0400; i++){
                if(i%128 == 0){
                    printf("\n");
                }
                if(isprint(device.cartridge.RAM[i])){
                    putchar(device.cartridge.RAM[i]);
                }
                else{
                    putchar('.');
                }

            }
            printf("\n");
            
        }
        else{
            device.cartridge.RAM = malloc(device.cartridge.RAMsize);
        }
#else
        // TODO implement a proper save file map on win32
        device.cartridge.RAM = malloc(device.cartridge.RAMsize);
#endif
        if(!device.cartridge.RAM){
            return false;
        }
    }
    else{
        device.cartridge.RAMsize = 0;
    }   
    device.cartridge.RAMEnabled = false;
    
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


    if(cart->ROMsize / 1000 != 0){
        printf("Rom size: %d MiB\n", cart->ROMsize % 1000);
    }
    else {
        printf("Rom size: %d KiB\n", cart->ROMsize);
    }
    if(cart->RAMsize / 1000 != 0){
        printf("Ram size: %d MiB\n", cart->RAMsize % 1000);
    }
    else {
        printf("Ram size: %d KiB\n", cart->RAMsize);
    }

}	

    
uint8_t ReadFromCartridge(uint16_t addr){
    uint8_t number_of_banks = device.cartridge.ROMsize / 16; // Each ROM bank is 16 KiB //

    if(number_of_banks > 2 && addr >= 0x4000){
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

uint8_t ReadFromExternalRAM(uint16_t addr){
    if (device.cartridge.RAMEnabled == true){
        printf("[INFO] Read from external RAM at: 0x%04X; current selected bank: 0x%02X\n", addr, device.cartridge.RAMnumber);
        
        uint8_t data = device.cartridge.RAM[(device.cartridge.RAMnumber * 0x2000) + (addr - 0xA000)];
        printf("[INFO] Read: 0x%02x\n", data);
        return data;
    }
    else {
        return 0xFF;
    }

}

void WriteToExternalRAM(uint16_t addr, uint8_t data){
    if(addr <= 0x1FFF){
        if((data & 0x0F) == 0x0A) device.cartridge.RAMEnabled = true;
        else device.cartridge.RAMEnabled = false;
        return;
    }
    if(device.cartridge.RAMEnabled && addr >= 0xA000 && addr <= 0xBFFF){
        //Write into ram
        printf("[INFO] Writing into external RAM at: 0x%04X with value: 0x%02x\n", addr, data);
        device.cartridge.RAM[(device.cartridge.RAMnumber * 0x2000) + (addr - 0xA000)] = data;
    }
    if(addr >= 0x4000 && addr <= 0x5FFF){
        // select RAM bank
        device.cartridge.RAMnumber = data;
        printf("[INFO] RAM BANK selected: 0x%02X\n", data);
    }

}
