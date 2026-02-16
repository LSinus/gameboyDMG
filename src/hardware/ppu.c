#include <stdlib.h>
#include "ppu.h"
#include "memory.h"
#include "device.h"

extern DEVICE device;

/* This function returns the current PPU mode */
uint8_t ppu_get_mode() {
    return device.memory[0xFF41] & 0x03;
}

/* basic initialization for ppu module */
void InitializePPU(){
    device.ppu.mode = MODE_2_OAM_SCAN;
    device.ppu.cycle_counter = 0;
    device.ppu.ly = 0;

    device.ppu.bg_enabled = true;
    device.ppu.wn_enabled = true;
    device.ppu.ob_enabled = true;
}




/* This function sets a mode for PPU reflecting the state inside the IO
   register visible at 0xFF41 on the memory bud
*/
void ppu_set_mode(PPU_MODE mode){
    device.ppu.mode = mode;
    device.memory[0xFF41] = (device.memory[0xFF41] & 0b11111100) | mode;
}


void ppu_lcd_off(){
    device.memory[0xFF44] = 0;
    device.ppu.ly = 0;
    device.ppu.wn_ly = 0;
    device.ppu.increment_wn_ly = false;
    ppu_set_mode(MODE_0_HBLANK);
}

bool ppu_lcd_get_on(){
    return (device.memory[0xFF40] & 0x80) != 0;
}

/* =========================== UTILITY FUNCTIONS FOR PPU SCANLINE ============================== */
/* all these function process the data for the corresponding layer and return the computed color */
/* number to use in the palette to retrieve the correct color to show on the screen              */

// check if the background is enabled
bool is_bg_enabled(){
    uint8_t LCDC = ReadMem(0xFF40);
    return device.ppu.bg_enabled && (LCDC & 0x01) != 0;
}

uint8_t ppu_scanline_bg(uint8_t x){
    uint8_t LCDC = ReadMem(0xFF40);
    /* It is important to look at SCY and SCX to map to world coordinates 
       in order to apply background scrolling. */
    uint8_t SCY = ReadMem(0xFF42);
    uint8_t SCX = ReadMem(0xFF43);


    uint8_t world_x = SCX + x;
    uint8_t world_y = (uint8_t)(SCY + device.ppu.ly); 

    // Tiles are 8x8 pixels so the index is the coordinates diveded by 8
    uint8_t tile_x  = world_x / 8;
    uint8_t tile_y  = world_y / 8;

    /* Tile id from VRAM Tile-Map. It is important to access memory array 
       directly here without using ReadMem because in this state the memory
       blocks reads in VRAM and OAM for CPU */
    uint16_t tile_map_addr = ((LCDC & 0b00001000) == 0 ? 0x9800 : 0x9C00); // Third bit of LCDC indicates the tile map location
    uint16_t tile_id_addr  = tile_map_addr + (tile_y * 32) + tile_x; // The address of the tile that is needed
    uint8_t  tile_id       = device.memory[tile_id_addr]; // The tile id picked directly from memory  

    uint16_t tile_data_addr;

    if((LCDC & 0x10) != 0){ // Use 0x8000 method (unsigned)
        tile_data_addr = 0x8000;
        tile_data_addr += tile_id * 16;
    }
    else{ // Use 0x8800 method (signed)
        tile_data_addr = 0x9000;
        tile_data_addr += ((int8_t)tile_id) * 16;
    }

    /* Every pixel is stored with 2 bits so in one byte there are 4 pixels. A row is 8 pixels so 2 bytes. */
    uint16_t tile_row_addr  = tile_data_addr + (world_y % 8) * 2; 

    /* The data is stored in two consecutive bytes, the first byte stores the least significant bit of the pixels 
       the second byte stores the most significant bit of the pixels  */

    uint8_t byte1 = device.memory[tile_row_addr];
    uint8_t byte2 = device.memory[tile_row_addr+1];

    uint8_t bit_index = 7 - (world_x % 8);

    uint8_t color_bit1 = (byte2 >> bit_index) & 1;
    uint8_t color_bit0 = (byte1 >> bit_index) & 1;

    return (color_bit1 << 1) | color_bit0;

}

// check if the window is enabled and if the current pixel is visible
bool is_wn_enabled(){
    uint8_t LCDC = ReadMem(0xFF40);
    // In DMG the bit 0 of LCDC that is bg/wn enable overrides the value of bit 5 if it is 0
    bool window_enabled = ((LCDC & 0x01) != 0 && (LCDC & 0x20) != 0);

    return (device.ppu.wn_enabled && window_enabled);
}

bool is_wn_visible(uint8_t x){
    uint8_t WY = ReadMem(0xFF4A);
    uint8_t WX = ReadMem(0xFF4B);
    return device.ppu.ly >= WY && x >= (WX - 7);
}

uint8_t ppu_scanline_wn(uint8_t x){
    uint8_t LCDC = ReadMem(0xFF40);
    uint8_t WY = ReadMem(0xFF4A);
    uint8_t WX = ReadMem(0xFF4B);

    uint8_t window_x = x - (WX - 7);
    uint8_t window_y = device.ppu.wn_ly;

    uint8_t tile_x  = window_x / 8;
    uint8_t tile_y  = window_y / 8;

    /* Read the address with different LCDC bit, bit 6, 
       the rest of calculation remains the same */
    uint16_t tile_map_addr = ((LCDC & 0x40) == 0 ? 0x9800 : 0x9C00);
    uint16_t tile_id_addr  = tile_map_addr + (tile_y * 32) + tile_x; 
    uint8_t  tile_id       = device.memory[tile_id_addr];   
    uint16_t tile_data_addr;

    if((LCDC & 0x10) != 0){ // Use 0x8000 method (unsigned)
        tile_data_addr = 0x8000;
        tile_data_addr += tile_id * 16;
    }
    else{ // Use 0x8800 method (signed)
        tile_data_addr = 0x9000;
        tile_data_addr += ((int8_t)tile_id) * 16;
    }

    /* Every pixel is stored with 2 bits so in one byte there are 4 pixels. A row is 8 pixels so 2 bytes. */
    uint16_t tile_row_addr  = tile_data_addr + (window_y % 8) * 2; 

    /* The data is stored in two consecutive bytes, the first byte stores the least significant bit of the pixels 
       the second byte stores the most significant bit of the pixels */

    uint8_t byte1 = device.memory[tile_row_addr];
    uint8_t byte2 = device.memory[tile_row_addr+1];

    uint8_t bit_index = 7 - (window_x % 8);

    uint8_t color_bit1 = (byte2 >> bit_index) & 1;
    uint8_t color_bit0 = (byte1 >> bit_index) & 1;

    // window is about to be rendered so signal the ppu to increment window internal line counter
    device.ppu.increment_wn_ly = true;

    return (color_bit1 << 1) | color_bit0;
}

bool is_obj_enabled(){
    uint8_t LCDC = ReadMem(0xFF40);
    return device.ppu.ob_enabled && (LCDC & 0x02) != 0;
}

/*uint8_t ppu_scanline_obj(){
    bool is_double_height = (LCDC & 0x04) != 0;

    for(size_t i = 0; i < device.ppu.visible_objects_counter; i++){
        // casting to uint8_t makes easier the access to each byte
        uint8_t *obj = (uint8_t *)&device.ppu.visible_objects[i]; 
        // x position is in byte 1
        if(x >= obj[1] - 8 && x < obj[1]) { // the object is under the current x pos

            // check if the tile is horizontally or vertically mirrored
            bool x_flip = (obj[3] & 0x20) != 0;
            bool y_flip = (obj[3] & 0x40) != 0;

            uint8_t y_in_tile = (device.ppu.ly - (obj[0] - 16)) % 8;
            uint8_t x_in_tile = x - (obj[1] - 8); 

            if(x_flip) x_in_tile = 7 - x_in_tile;       
            if(y_flip) y_in_tile = 7 - y_in_tile; //TODO check the correctness of flipping for y axis

            if(is_double_height){ // in this case it is important to understand which tile to fetch
                if(device.ppu.ly - (obj[0] - 16) < 8){ // fetch upper tile
                    if(y_flip) tile_id = obj[2] | 0x01;
                    else tile_id = obj[2] & 0xFE;
                }
                else{ // fetch bottom tile
                    if(y_flip) tile_id = obj[2] & 0xFE;
                    else tile_id = obj[2] | 0x01;
                }
            }
            else{
                tile_id = obj[2];
            }

            tile_data_addr = 0x8000 + tile_id * 16;

            // check priority 0 = high, 1 = low
            bool priority = (obj[3] & 0x80) == 0; 

            // Every pixel is stored with 2 bits so in one byte there are 4 pixels. A row is 8 pixels so 2 bytes. 
            tile_row_addr  = tile_data_addr + (y_in_tile % 8) * 2; 

            // The data is stored in two consecutive bytes, the first byte stores the least significant bit of the pixels 
             //  the second byte stores the most significant bit of the pixels /

            byte1 = device.memory[tile_row_addr];
            byte2 = device.memory[tile_row_addr+1];

            bit_index = 7 - (x_in_tile % 8);

            color_bit1 = (byte2 >> bit_index) & 1;
            color_bit0 = (byte1 >> bit_index) & 1;

            obj_color_number = (color_bit1 << 1) | color_bit0;
        }
    }
}*/


/* This function gets data from VRAM and sends it to LCD framebuffer at the end 
   of the execution of this function a new line is visible on the screen. */
void ppu_scanline(){
    uint8_t bg_color_number   = 0;
    uint8_t wn_color_number   = 0;
    uint8_t obj_color_number = 0;
    uint8_t color = 0;
    uint8_t tile_x;
    uint8_t tile_y;
    uint16_t tile_map_addr;
    uint16_t tile_data_addr;
    uint16_t tile_row_addr;
    uint16_t tile_id_addr;
    uint8_t tile_id;
    uint8_t byte1;
    uint8_t byte2;
    uint8_t bit_index;
    uint8_t color_bit0;
    uint8_t color_bit1;
    uint8_t BGP = ReadMem(0xFF47);
    // cycle for an entire line
    for(uint8_t x = 0; x < WINDOW_WIDTH; x++){
        uint8_t LCDC = ReadMem(0xFF40);
        
        /* --- SECTION FOR BACKGROUND LAYER --- */
        if(is_bg_enabled()){
            bg_color_number = ppu_scanline_bg(x);

            /* Now based on the color number it is possible to get the right value from BG palette */
            color = (BGP >> (bg_color_number * 2)) & 0x03;
            if(device.ppu.ly <= 1 && device.ppu.debug){
                printf("background at   x = %d y = %d color = %d\n", x, device.ppu.ly, color);
            }
        }           

        /* --- SECTION FOR WINDOW LAYER --- */
        if(is_wn_enabled() && is_wn_visible(x)){
            wn_color_number = ppu_scanline_wn(x);
            /* Now based on the color number it is possible to get the right value from BG palette window uses same palette of bg */ color = (BGP >> (wn_color_number * 2)) & 0x03;
        }

        /* --- SECTION FOR SPRITES --- */
        if(is_obj_enabled()){

            bool is_double_height = (LCDC & 0x04) != 0;
            for(size_t i = 0; i < device.ppu.visible_objects_counter; i++){
                // casting to uint8_t makes easier the access to each byte
                uint8_t *obj = (uint8_t *)&device.ppu.visible_objects[i]; 
                // x position is in byte 1
                if(x >= obj[1] - 8 && x < obj[1]) { // the object is under the current x pos


                    // check if the tile is horizontally or vertically mirrored
                    bool x_flip = (obj[3] & 0x20) != 0;
                    bool y_flip = (obj[3] & 0x40) != 0;

                    uint8_t y_in_tile = (device.ppu.ly - (obj[0] - 16)) % 8;
                    uint8_t x_in_tile = x - (obj[1] - 8); 

                    if(x_flip) x_in_tile = 7 - x_in_tile;       
                    if(y_flip) y_in_tile = 7 - y_in_tile; //TODO check the correctness of flipping for y axis

                    if(is_double_height){ // in this case it is important to understand which tile to fetch
                        if(device.ppu.ly - (obj[0] - 16) < 8){ // fetch upper tile
                            if(y_flip) tile_id = obj[2] | 0x01;
                            else tile_id = obj[2] & 0xFE;
                        }
                        else{ // fetch bottom tile
                            if(y_flip) tile_id = obj[2] & 0xFE;
                            else tile_id = obj[2] | 0x01;
                        }
                    }
                    else{
                        tile_id = obj[2];
                    }

                    tile_data_addr = 0x8000 + tile_id * 16;

                    // check priority 0 = high, 1 = low
                    bool priority = (obj[3] & 0x80) == 0; 

                    /* Every pixel is stored with 2 bits so in one byte there are 4 pixels. A row is 8 pixels so 2 bytes. */
                    tile_row_addr  = tile_data_addr + (y_in_tile % 8) * 2; 

                    /* The data is stored in two consecutive bytes, the first byte stores the least significant bit of the pixels 
                       the second byte stores the most significant bit of the pixels */

                    byte1 = device.memory[tile_row_addr];
                    byte2 = device.memory[tile_row_addr+1];

                    bit_index = 7 - (x_in_tile % 8);

                    color_bit1 = (byte2 >> bit_index) & 1;
                    color_bit0 = (byte1 >> bit_index) & 1;

                    // Determine the final underlying color before checking sprites
                    uint8_t underlying_color_number;

                    if(is_bg_enabled()) underlying_color_number = bg_color_number;
                    if(is_wn_enabled() && is_wn_visible(x)) underlying_color_number = wn_color_number;

                    obj_color_number = (color_bit1 << 1) | color_bit0;
                    if (obj_color_number == 0 || (!priority && underlying_color_number != 0)) {
                        continue;
                    }

                    /* Now based on the color number it is possible to get the right value from OBP0 palette */
                    uint8_t palette;
                    if((obj[3] & 0x10) == 0) palette = ReadMem(0xFF48); // OBP0
                    else palette = ReadMem(0xFF49); // OBP1

                    if(device.ppu.ob_enabled){
                        color = (palette >> (obj_color_number * 2)) & 0x03;
                    }
                    break;
                }  

            }}
        
        if(device.ppu.process_frame_buffer){
            device.ppu.process_frame_buffer(x, device.ppu.ly, color);
        }
    }

}

/* Comparator used by qsort in order to sort sprites for x coordinate value */
int sprite_comparator(const void *a, const void *b){
    uint8_t *_a = (uint8_t*)a;
    uint8_t *_b = (uint8_t*)b;
    return _a[1] - _b[1];
}

/* This function check all 40 sprites during OAM Scan mode in order to find the 10
 * spirtes that overlaps the y coordinate of the current scanline. 
 */
void ppu_oam_scan(){
    device.ppu.visible_objects_counter = 0;
    /* Each object is 4 bytes in memory so let's read the memory as uint32_t values */
    uint32_t *obj_base_addr_ptr = (uint32_t *)&(device.memory[0xFE00]);
    uint32_t *obj_end_addr_ptr  = (uint32_t *)&(device.memory[0xFE9F]);

    uint8_t LCDC = device.memory[0xFF40];
    bool is_double_height = (LCDC & 0x04) != 0;
    uint8_t *obj;
    for(uint32_t *i = obj_base_addr_ptr; i < obj_end_addr_ptr; i++){
        // cast to an array of four entries
        obj = (uint8_t *)i;
        // first byte Y pos
        uint8_t obj_height = is_double_height ? 16 : 8;
        if(device.ppu.ly >= obj[0] - 16 && device.ppu.ly < obj[0] - 16 + obj_height){ // visible for this scanline
            device.ppu.visible_objects[device.ppu.visible_objects_counter++] = *i;
            if(device.ppu.visible_objects_counter == 10) break; // max 10 visible objects for scanline
        }
        
    }

    // TODO maybe change with counting sort
    qsort(device.ppu.visible_objects, device.ppu.visible_objects_counter, sizeof(uint32_t), sprite_comparator); 
}


/* This function performs a step of an amount of clock cycles in the 
 * PPU state machine. The purpose is to emulate correctly this behaviour 
 * after the CPU has exectuted an instruction that takes an amount of time 
*/
void ppu_step(int cycles){
    // PPU is off
    if(!ppu_lcd_get_on()) return;

    device.ppu.cycle_counter += cycles;

    uint8_t STAT   = device.memory[0xFF41];

    switch(device.ppu.mode){
        case MODE_2_OAM_SCAN:
            if(device.ppu.cycle_counter >= 80){
                device.ppu.cycle_counter -= 80;
                ppu_set_mode(MODE_3_DRAWING);
            }
            break;
        case MODE_3_DRAWING:
            if(device.ppu.cycle_counter >= 172){
                device.ppu.cycle_counter -= 172;
                ppu_set_mode(MODE_0_HBLANK);
                // check if in STAT an interrupt for this event has to be requested
                if((STAT & 0x08) != 0) device.memory[IF_REG] |= 0x02; // request STAT interrupt
                ppu_scanline();
            }
            break;
        case MODE_0_HBLANK:
            if (device.ppu.cycle_counter >= 204) {
                device.ppu.cycle_counter -= 204;
                device.ppu.ly++;
                if(device.ppu.increment_wn_ly) device.ppu.wn_ly++;
                device.memory[0xFF44] = device.ppu.ly;
                uint8_t LYC    = device.memory[0xFF45];
                

                if(device.ppu.ly == LYC){
                    // Set coincidence Flag (second bit in stat)
                    device.memory[0xFF41] |= 0x04;
                    STAT = device.memory[0xFF41];
                    
                    // Check if the interrupt for this event is enabled (bit 6)
                    if((STAT & 0x40) != 0){
                        device.memory[IF_REG] |= 0x02; // request STAT interrupt
                    }
                } else{
                        device.memory[0xFF41] &= ~0x04;
                        STAT = device.memory[0xFF41];
                }

                if (device.ppu.ly == 144) {
                    ppu_set_mode(MODE_1_VBLANK);
                    // check if in STAT an interrupt for this event has to be requested
                    if((STAT & 0x10) != 0) device.memory[IF_REG] |= 0x02; // request STAT interrupt
                    // Request V-Blank interrupt
                    device.memory[IF_REG] |= 0x1; // Interrupt flag
                } else {
                    ppu_set_mode(MODE_2_OAM_SCAN);
                    device.ppu.increment_wn_ly = false;
                    ppu_oam_scan();
                    // check if in STAT an interrupt for this event has to be requested
                    if((STAT & 0x20) != 0) device.memory[IF_REG] |= 0x02; // request STAT interrupt
                }
            }
            break;
        /*
        case MODE_1_VBLANK:
            if (device.ppu.ly == 153 && device.ppu.cycle_counter >= 4) {
                device.ppu.ly = 0;
                device.memory[0xFF44] = device.ppu.ly;
            }

            if (device.ppu.cycle_counter >= 456) { // One scanline worth of time
                device.ppu.cycle_counter -= 456;

                if (device.ppu.ly == 0) {
                    device.ppu.ly = 0;
                    device.memory[0xFF44] = 0;
                    ppu_set_mode(MODE_2_OAM_SCAN);
                    ppu_oam_scan();
                    // check if in STAT an interrupt for this event has to be requested
                    if((STAT & 0x20) != 0) device.memory[IF_REG] |= 0x02; // request STAT interrupt
                    return;
                }

                device.ppu.ly++;
                device.memory[0xFF44] = device.ppu.ly;

                
            }
            break;*/
        case MODE_1_VBLANK:
            if (device.ppu.cycle_counter >= 456) { // One scanline worth of time
                device.ppu.cycle_counter -= 456;
                device.ppu.ly++;
                device.memory[0xFF44] = device.ppu.ly;

                if (device.ppu.ly > 153) {
                    device.ppu.ly = 0;
                    device.ppu.increment_wn_ly = false;
                    device.ppu.wn_ly = 0;
                    device.memory[0xFF44] = 0;
                    ppu_set_mode(MODE_2_OAM_SCAN);
                    ppu_oam_scan();
                    // check if in STAT an interrupt for this event has to be requested
                    if((STAT & 0x20) != 0) device.memory[IF_REG] |= 0x02; // request STAT interrupt
                }
            }
            break;
    }
}
