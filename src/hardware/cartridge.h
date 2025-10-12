

#ifndef CARTRIDGE_H
#define CARTRIDGE_H

typedef struct CARTRIDGE{
    const char *title;
    const char *type;
    char *ROMSize;
    char *RAMSize;
    // TODO IMPLEMENT THE CARTRIDGE HEADER AND MBC
} CARTRIDGE;

/* Cartridge ROM type */
extern const char* cartridge_types[256];

#endif