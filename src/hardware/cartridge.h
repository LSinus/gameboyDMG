#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define SAVES_PATH "saves/"

/* The data pointer points to a dinamically allocated 
 * memory buffer in wich all the data contained in the 
 * rom file of the cartridge is copied at the beginning
 * of the program
 */
typedef struct CARTRIDGE{
    uint8_t *data;
    uint8_t *RAM;
    char *gamePath;
    const char *title;
    const char *type;

    // Sizes are specified in KiB
    uint16_t ROMsize;
    uint16_t RAMsize;
    bool RAMEnabled;
    // This number indicates the current selected rom bank if any
    uint8_t ROMnumber;
    // This number indicates the current selected ram bank if any
    uint8_t RAMnumber;
} CARTRIDGE;

/* Cartridge ROM type */
extern const char* cartridge_types[256];

/* This function initialize the cartridge
 * taking the name of the rom to load
 * if something goes wrong it return false,
 * if all goes well it returns true
 */ 
bool InitializeCartridge(char *game_path);

/* This function makes possible to obtain the effect of
 * the memory bank controller, in practice it reads from
 * the dinamically allocated buffer inside the cartridge
 * and it is called from the main ReadMem inside memory.h
 * that is a sort of dispatcher for all the other components
 * this function is called whenever the address put inside
 * the memory bus is recognized as a part of the cartrdige
 */ 
uint8_t ReadFromCartridge(uint16_t addr);

/* This function is used to write in MBC register
 * whenever the main WriteMem in memory.h is called
 * if the address belongs to the cartridge this function
 * is invoked implementing the logic of the current MBC 
 * used in the cartridge
 * */
void WriteToCartridge(uint16_t addr, uint8_t data);
void WriteToExternalRAM(uint16_t addr, uint8_t data);
uint8_t ReadFromExternalRAM(uint16_t addr);

uint16_t get_rom_size();
uint16_t get_ram_size();

/* This function prints a formatted text with cartrdige infos */
void PrintCartridgeInfo();

#endif
