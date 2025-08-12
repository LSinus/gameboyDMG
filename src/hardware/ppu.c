#include "ppu.h"
#include "memory.h"


/* This function returns the current PPU mode */
uint8_t ppu_get_mode() {
    return memory[0xFF41] & 0x03;
}



/* This function sets a mode for PPU reflecting the state inside the IO
   register visible at 0xFF41 on the memory bud
*/
void ppu_set_mode(PPU *ppu, PPU_MODE mode){
    ppu->mode = mode;
    memory[0xFF41] = (memory[0xFF41] & 0b11111100) | mode;
}



/* This function gets data from VRAM and sends it to LCD framebuffer at the end 
   of the execution of this function a new line is visible on the screen. */
void ppu_scanline(PPU *ppu){
    uint8_t w_color_number   = 0;
    uint8_t bg_color_number  = 0;
    uint8_t obj_color_number = 0;

    // cycle for an entire line
    for(uint8_t x = 0; x < WINDOW_WIDTH; x++){
        uint8_t LCDC = ReadMem(0xFF40);
        
        /* --- SECTION FOR BACKGROUND LAYER --- */
        /* It is important to look at SCY and SCX to map to world coordinates 
           in order to apply background scrolling. */
        uint8_t SCY = ReadMem(0xFF42);
        uint8_t SCX = ReadMem(0xFF43);


        uint8_t world_x = SCX + x;
        uint8_t world_y = SCY + ppu->ly; 

        // Tiles are 8x8 pixels so the index is the coordinates diveded by 8
        uint8_t tile_x  = world_x / 8;
        uint8_t tile_y  = world_y / 8;

        /* Tile id from VRAM Tile-Map. It is important to access memory array 
           directly here without using ReadMeme because in this state the memory
           blocks reads in VRAM and OAM for CPU */
        uint16_t tile_map_addr = ((LCDC & 0b00001000) == 0 ? 0x9800 : 0x9C00); // Third bit of LCDC indicates the tile map location
        uint16_t tile_id_addr  = tile_map_addr + (tile_y * 32) + tile_x; // The address of the tile that is needed
        uint8_t  tile_id       = memory[tile_id_addr]; // The tile id picked directly from memory  

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
        
        uint8_t byte1 = memory[tile_row_addr];
        uint8_t byte2 = memory[tile_row_addr+1];

        uint8_t bit_index = 7 - (world_x % 8);

        uint8_t color_bit1 = (byte2 >> bit_index) & 1;
        uint8_t color_bit0 = (byte1 >> bit_index) & 1;

        bg_color_number = (color_bit1 << 1) | color_bit0;

        /* Now based on the color number it is possible to get the right value from BG palette */
        uint8_t BGP = ReadMem(0xFF47); 
        uint8_t color = (BGP >> (bg_color_number * 2)) & 0x03;
        

        /* --- SECTION FOR WINDOW LAYER --- */
        uint8_t WY = ReadMem(0xFF4A);
        uint8_t WX = ReadMem(0xFF4B);
        bool window_enabled = (LCDC & 0x20) != 0;

        // check if the window is enabled and if the current pixel is visible
        
        if(window_enabled && ppu->ly >= WY && x >= (WX - 7)){
            uint8_t window_x = x - (WX - 7);
            uint8_t window_y = ppu->ly - WY;
            
            tile_x  = window_x / 8;
            tile_y  = window_y / 8;

            /* Read the address with different LCDC bit, bit 6, 
               the rest of calculation remains the same */
            tile_map_addr = ((LCDC & 0b01000000) == 0 ? 0x9800 : 0x9C00);
            tile_id_addr  = tile_map_addr + (tile_y * 32) + tile_x; 
            tile_id       = memory[tile_id_addr];   

            if((LCDC & 0x10) != 0){ // Use 0x8000 method (unsigned)
                tile_data_addr = 0x8000;
                tile_data_addr += tile_id * 16;
            }
            else{ // Use 0x8800 method (signed)
                tile_data_addr = 0x9000;
                tile_data_addr += ((int8_t)tile_id) * 16;
            }

            /* Every pixel is stored with 2 bits so in one byte there are 4 pixels. A row is 8 pixels so 2 bytes. */
            tile_row_addr  = tile_data_addr + (window_y % 8) * 2; 

            /* The data is stored in two consecutive bytes, the first byte stores the least significant bit of the pixels 
            the second byte stores the most significant bit of the pixels */
            
            byte1 = memory[tile_row_addr];
            byte2 = memory[tile_row_addr+1];

            bit_index = 7 - (window_x % 8);

            color_bit1 = (byte2 >> bit_index) & 1;
            color_bit0 = (byte1 >> bit_index) & 1;

            w_color_number = (color_bit1 << 1) | color_bit0;

            /* Now based on the color number it is possible to get the right value from BG palette */
            color = (BGP >> (w_color_number * 2)) & 0x03;
        }
        


        /* --- SECTION FOR SPRITES --- */
        
        bool obj_enabled = (LCDC & 0x02) != 0;
        if(obj_enabled){
            bool is_double_height = (LCDC & 0x04) != 0;
            for(size_t i = 0; i < ppu->visible_objects_counter; i++){
                // casting to uint8_t makes easier the access to each byte
                uint8_t *obj = (uint8_t*)&ppu->visible_objects[i]; 
                // x position is in byte 1
                if(x >= obj[1] - 8 && x < obj[1]) { // the object is under the current x pos
                    
                    if(is_double_height){ // in this case it is important to understand which tile to fetch
                        if(ppu->ly - (obj[0] - 16) < 8){ // fetch upper tile
                            tile_id = obj[2] & 0xFE;
                        }
                        else{ // fetch bottom tile
                            tile_id = obj[2] | 0x01;
                        }
                    }
                    else{
                        tile_id = obj[2];
                    }


                    tile_data_addr = 0x8000 + tile_id * 16;

                    // check if the tile is horizontally or vertically mirrored
                    bool x_flip = (obj[3] & 0x20) != 0;
                    bool y_flip = (obj[3] & 0x40) != 0;

                    uint8_t y_in_tile = (ppu->ly - (obj[0] - 16)) % 8;
                    uint8_t x_in_tile = x - (obj[1] - 8); 

                    if(x_flip) x_in_tile = 7 - x_in_tile;       
                    if(y_flip) y_in_tile = 7 - y_in_tile;

                    // check priority 0 = high, 1 = low
                    bool priority = (obj[3] & 0x80) == 0; 

                    /* Every pixel is stored with 2 bits so in one byte there are 4 pixels. A row is 8 pixels so 2 bytes. */
                    tile_row_addr  = tile_data_addr + (y_in_tile % 8) * 2; 

                    /* The data is stored in two consecutive bytes, the first byte stores the least significant bit of the pixels 
                    the second byte stores the most significant bit of the pixels */
                    
                    byte1 = memory[tile_row_addr];
                    byte2 = memory[tile_row_addr+1];

                    bit_index = 7 - (x_in_tile % 8);

                    color_bit1 = (byte2 >> bit_index) & 1;
                    color_bit0 = (byte1 >> bit_index) & 1;

                    // Determine the final underlying color before checking sprites
                    uint8_t underlying_color_number = bg_color_number;
                    if (window_enabled && ppu->ly >= WY && x >= (WX - 7)) {
                        underlying_color_number = w_color_number;
                    }

                    obj_color_number = (color_bit1 << 1) | color_bit0;
                    if (obj_color_number == 0 || (!priority && underlying_color_number != 0)) {
                        continue;
                    }

                    /* Now based on the color number it is possible to get the right value from OBP0 palette */
                    uint8_t palette;
                    if((obj[3] & 0x10) == 0) palette = ReadMem(0xFF48); // OBP0
                    else palette = ReadMem(0xFF49); // OBP1

                    color = (palette >> (obj_color_number * 2)) & 0x03;
                    break;
                }  
            }
        }
        
        ppu->process_frame_buffer(x, ppu->ly, color);
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
void ppu_oam_scan(PPU *ppu){
    ppu->visible_objects_counter = 0;
    /* Each object is 4 bytes in memory so let's read the memory as uint32_t values */
    uint32_t *obj_base_addr_ptr = (uint32_t *)&memory[0xFE00];
    uint32_t *obj_end_addr_ptr  = (uint32_t *)&memory[0xFE9F];

    uint8_t LCDC = memory[0xFF40];
    bool is_double_height = (LCDC & 0x04) != 0;
    uint8_t *obj;
    for(uint32_t *i = obj_base_addr_ptr; i < obj_end_addr_ptr; i++){
        // cast to an array of four entries
        obj = (uint8_t *)i;
        // first byte Y pos
        uint8_t obj_height = is_double_height ? 16 : 8;
        if(ppu->ly >= obj[0] - 16 && ppu->ly < obj[0] - 16 + obj_height){ // visible for this scanline
            ppu->visible_objects[ppu->visible_objects_counter++] = *i;
            if(ppu->visible_objects_counter == 10) break; // max 10 visible objects for scanline
        }
        
    }

    // TODO maybe change with counting sort
    qsort(ppu->visible_objects, ppu->visible_objects_counter, sizeof(uint32_t), sprite_comparator); 
}


/* This function performs a step of an amount of clock cycles in the 
 * PPU state machine. The purpose is to emulate correctly this behaviour 
 * after the CPU has exectuted an instruction that takes an amount of time 
*/
void ppu_step(PPU *ppu, int cycles){
    ppu->cycle_counter += cycles;

    uint8_t STAT   = memory[0xFF41];

    switch(ppu->mode){
        case MODE_2_OAM_SCAN:
            if(ppu->cycle_counter >= 80){
                ppu->cycle_counter -= 80;
                ppu_set_mode(ppu, MODE_3_DRAWING);
            }
            break;
        case MODE_3_DRAWING:
            if(ppu->cycle_counter >= 172){
                ppu->cycle_counter -= 172;
                ppu_set_mode(ppu, MODE_0_HBLANK);
                // check if in STAT an interrupt for this event has to be requested
                if((STAT & 0x08) != 0) memory[IF_REG] |= 0x02; // request STAT interrupt
                ppu_scanline(ppu);
            }
            break;
        case MODE_0_HBLANK:
            if (ppu->cycle_counter >= 204) {
                ppu->cycle_counter -= 204;
                ppu->ly++;
                memory[0xFF44] = ppu->ly;
                uint8_t LYC    = memory[0xFF45];
                

                if(ppu->ly == LYC){
                    // Set coincidence Flag (second bit in stat)
                    memory[0xFF41] |= 0x04;
                    STAT = memory[0xFF41];
                    
                    // Check if the interrupt for this event is enabled (bit 6)
                    if((STAT & 0x40) != 0){
                        memory[IF_REG] |= 0x02; // request STAT interrupt
                    }
                } else{
                        memory[0xFF41] &= ~0x04;
                        STAT = memory[0xFF41];
                }

                if (ppu->ly == 144) {
                    ppu_set_mode(ppu, MODE_1_VBLANK);
                    // check if in STAT an interrupt for this event has to be requested
                    if((STAT & 0x10) != 0) memory[IF_REG] |= 0x02; // request STAT interrupt
                    // Request V-Blank interrupt
                    memory[IF_REG] |= 0x1; // Interrupt flag
                } else {
                    ppu_set_mode(ppu, MODE_2_OAM_SCAN);
                    ppu_oam_scan(ppu);
                    // check if in STAT an interrupt for this event has to be requested
                    if((STAT & 0x20) != 0) memory[IF_REG] |= 0x02; // request STAT interrupt
                }
            }
            break;
        case MODE_1_VBLANK:
            if (ppu->cycle_counter >= 456) { // One scanline worth of time
                ppu->cycle_counter -= 456;
                ppu->ly++;
                memory[0xFF44] = ppu->ly;

                if (ppu->ly > 153) {
                    ppu->ly = 0;
                    memory[0xFF44] = 0;
                    ppu_set_mode(ppu, MODE_2_OAM_SCAN);
                    ppu_oam_scan(ppu);
                    // check if in STAT an interrupt for this event has to be requested
                    if((STAT & 0x20) != 0) memory[IF_REG] |= 0x02; // request STAT interrupt
                }
            }
            break;
    }
}
