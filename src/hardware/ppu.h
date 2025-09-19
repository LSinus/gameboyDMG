#ifndef PPU_H
#define PPU_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WINDOW_WIDTH 160 
#define WINDOW_HEIGHT 144

#define SCALE_FACTOR 5 // Output window scale factor

#define USER_WINDOW_WIDTH WINDOW_WIDTH * SCALE_FACTOR
#define USER_WINDOW_HEIGHT WINDOW_HEIGHT * SCALE_FACTOR

typedef enum {
    MODE_0_HBLANK,
    MODE_1_VBLANK,
    MODE_2_OAM_SCAN,
    MODE_3_DRAWING
} PPU_MODE;

/* Definition of PPU state machine */
typedef struct PPU {
    PPU_MODE mode;
    uint8_t ly;
    uint8_t visible_objects_counter;
    size_t cycle_counter;
    uint32_t visible_objects[10];

    void (*process_frame_buffer)(int x, int y, uint8_t color);
} PPU;


void ppu_step(int cycles);
void ppu_oam_scan();
void ppu_scanline();
void ppu_set_mode(PPU_MODE mode);
uint8_t ppu_get_mode();


#endif