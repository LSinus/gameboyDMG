#ifndef PPU_H
#define PPU_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define WINDOW_WIDTH 160 
#define WINDOW_HEIGHT 144

#define SCALE_FACTOR 4 // Output window scale factor

#define USER_WINDOW_WIDTH (WINDOW_WIDTH * SCALE_FACTOR)
#define USER_WINDOW_HEIGHT (WINDOW_HEIGHT * SCALE_FACTOR)

#define ppu_set_ly(n)\
    do{\
        device.ppu.ly = (n);\
        device.memory[0xFF44] = (n);\
    }while(0)

#define ppu_increment_ly()\
    ppu_set_ly(device.ppu.ly+1)



typedef enum {
    MODE_0_HBLANK = 0,
    MODE_1_VBLANK,
    MODE_2_OAM_SCAN,
    MODE_3_DRAWING
} PPU_MODE;

/* Definition of PPU state machine */
typedef struct PPU {
    size_t cycle_counter;
    uint8_t ly;
    uint8_t wn_ly;
    uint8_t visible_objects_counter;
    PPU_MODE mode;

    /* enabled in the sense of the emulator
     * they are computed but not showed on
     * the screen
     */
    bool bg_enabled;
    bool wn_enabled;
    bool ob_enabled;
    bool increment_wn_ly;
    bool debug;

    /* sprite object visible in the current scanline */
    uint32_t visible_objects[10];

    void (*process_frame_buffer)(int x, int y, uint8_t color);
} PPU;

void InitializePPU();
void ppu_step(int cycles);
void ppu_oam_scan();
void ppu_scanline();
void ppu_set_mode(PPU_MODE mode);
uint8_t ppu_get_mode();
void ppu_lcd_off();
bool ppu_lcd_get_on();

#endif
