#include <stdlib.h>
#include "device.h"
#include "debugger.h"
#include <unistd.h>

#define ST_FILE_HEADER "LSGBSTATFILEHDR"
#define advance(n)\
    do{\
        if(*hwsnap_size < n) printf("Error: Unexpected end of file\n");\
        else{\
            (*hwsnap_buf) += n;\
                           (*hwsnap_size) -= n;\
        }\
    } while(0)

DEVICE device = {0};

static void ppu_get_string_mode(char *ppu_mode) {
    switch(device.ppu.mode) {
        case MODE_0_HBLANK:   strcpy(ppu_mode, "HBLANK");   ppu_mode[6] = 0; break;
        case MODE_1_VBLANK:   strcpy(ppu_mode, "VBLANK");   ppu_mode[6] = 0; break;
        case MODE_2_OAM_SCAN: strcpy(ppu_mode, "OAM_SCAN"); ppu_mode[8] = 0; break;
        case MODE_3_DRAWING:  strcpy(ppu_mode, "DRAWING");  ppu_mode[7] = 0; break;
    }
}

static void interrupts_get_string_stat(char *buf, size_t buf_size) {
    uint8_t ie = device.memory[IE_REG];
    uint8_t _if = device.memory[IF_REG]; 

    snprintf(buf, buf_size,
            "IME: %s\n"
            "      [J] [S] [T] [L] [V]\n"
            "IE:    %d   %d   %d   %d   %d\n"
            "IF:    %d   %d   %d   %d   %d",
            device.cpu.IME ? "ON" : "OFF",
            (ie >> 4) & 1, (ie >> 3) & 1, (ie >> 2) & 1, (ie >> 1) & 1, ie & 1,
            (_if >> 4) & 1, (_if >> 3) & 1, (_if >> 2) & 1, (_if >> 1) & 1, _if & 1);
}

/* Copies the status of the emulator inside the buffer, the provided buffer 
   must be at least 82 bytes
*/
void GetEmulatorStatus(char* buf, size_t buf_size){
    uint8_t a =  device.cpu.AF >> 8;
    uint8_t f = (device.cpu.AF & 0xFF);
    uint8_t b =  device.cpu.BC >> 8;
    uint8_t c = (device.cpu.BC & 0xFF);
    uint8_t d =  device.cpu.DE >> 8;
    uint8_t e = (device.cpu.DE & 0xFF);
    uint8_t h =  device.cpu.HL >> 8;
    uint8_t l = (device.cpu.HL & 0xFF);

    char formatted_f_reg[5] = {'-', '-', '-', '-', 0};

    if(f & 0x80) formatted_f_reg[0] = 'Z';
    if(f & 0x40) formatted_f_reg[1] = 'N';
    if(f & 0x20) formatted_f_reg[2] = 'H';
    if(f & 0x10) formatted_f_reg[3] = 'C';

    char interrupts_stat[256] = {0};
    interrupts_get_string_stat(interrupts_stat, sizeof(interrupts_stat));

    char ppu_mode[50] = {0};
    ppu_get_string_mode(ppu_mode);

    char tmp[64];
    emulator_disassemble(device.cpu.PC, tmp, sizeof(tmp));

    snprintf(buf, buf_size, 
            "CPU:\n" 
            "A 0x%02x B 0x%02X C: 0x%02X D: 0x%02X E: 0x%02X H: 0x%02X L: 0x%02X\nSP: 0x%04X PC: 0x%04X\n"
            "F %s\n"
            "Next opcodes: (%02X %02X %02X %02X)\n"
            "Instruction: \n "
            "%s\n\n"
            "Halted: %d    Stopped: %d\n"
            "\n"
            "Interrupts:\n"
            "%s\n"
            "\n"
            "PPU:\n"
            "mode:%s \n"
            "ly:%u"
            "\n\n"
            "DMA:\n"
            "status: %s\n"
            "cycles: %llu",
            a, b, c, d, e, h, l, device.cpu.SP, device.cpu.PC, 
            formatted_f_reg,
            ReadMem(device.cpu.PC), ReadMem(device.cpu.PC+1), ReadMem(device.cpu.PC+2), ReadMem(device.cpu.PC+3), 
            tmp,
            device.cpu.halted, !device.cpu.running,
            interrupts_stat,
            ppu_mode,
            device.ppu.ly,
            device.dma.running ? "active" : "not active",
            device.dma.cycles
            );
}

void GetEmulatorStatusFile(char* buf, size_t buf_size){
    uint8_t a =  device.cpu.AF >> 8;
    uint8_t f = (device.cpu.AF & 0xFF);

    char formatted_f_reg[5] = {'-', '-', '-', '-', 0};

    if(f & 0x80) formatted_f_reg[0] = 'Z';
    if(f & 0x40) formatted_f_reg[1] = 'N';
    if(f & 0x20) formatted_f_reg[2] = 'H';
    if(f & 0x10) formatted_f_reg[3] = 'C';

    char tmp[64];
    emulator_disassemble(device.cpu.PC, tmp, sizeof(tmp));
    uint8_t lcdc = device.memory[0xFF40];
    char enable = (lcdc & 0x80) == 0 ? '-' : '+';
    snprintf(buf, buf_size,"A:%02x F:%s BC:%04x DE:%04x HL:%04x SP:%04x PC:%04x ppu:%c%u LY:%u |%s    [$ff93]: %02x\n",
            a, formatted_f_reg, device.cpu.BC, device.cpu.DE, device.cpu.HL, device.cpu.SP, device.cpu.PC, 
            enable, ppu_get_mode(), device.ppu.ly, tmp, device.memory[0xff93]);
}

/* This function allows the cpu to correctly handle interrupts */
int handleInterrupts(){
    uint8_t IE = ReadMem(IE_REG);
    uint8_t IF = ReadMem(IF_REG);

    uint8_t requested = IE & IF;

    if(!device.cpu.IME){
        if(requested != 0) device.cpu.halted = false; // pending interrupt wakes up cpu
        // EI instruction just happened, let's enable IME for the next one
        if(device.cpu.request_IME) {
            device.cpu.request_IME = false;
            device.cpu.IME = true;
        }
        return 0;
    }


    if (requested == 0) {
        return 0;
    }

    // An interrupt is happening, so the CPU is no longer halted
    device.cpu.halted = false;
    device.cpu.IME = false; // Disable further interrupts

    // Push PC to the stack
    device.cpu.SP -= 2;
    WriteMem(device.cpu.SP, (uint8_t)(device.cpu.PC & 0xFF));
    WriteMem(device.cpu.SP + 1, (uint8_t)(device.cpu.PC >> 8));

    // Check interrupts in order of priority
    if (requested & 0x01) { // V-Blank
        device.memory[IF_REG] &= ~0x01; // Clear the request flag
        device.cpu.PC = 0x0040;
    } else if (requested & 0x02) { // LCD STAT
        device.memory[IF_REG] &= ~0x02;
        device.cpu.PC = 0x0048;
    } else if (requested & 0x04) { // Timer
        device.memory[IF_REG] &= ~0x04;
        device.cpu.PC = 0x0050;
    } else if (requested & 0x08) { // Serial
        device.memory[IF_REG] &= ~0x08;
        device.cpu.PC = 0x0058;
    } else if (requested & 0x10) { // Joypad
        device.memory[IF_REG] &= ~0x10;
        printf("joypad interrupt cleared\n");
        device.cpu.PC = 0x0060;
    }

    return 20;
}

void InitializePowerOnState(){
    InitializeCPU(); 
    device.boot_rom_enabled = true;

    // Initialize PPU state properly
    InitializePPU();
    
    // Initialize Serial port 
    device.serial_port.cycles_elapsed = 0;
    device.serial_port.transfer_in_progress = false;

    // Initialize I/O registers
    device.memory[0xFF00] = 0xCF; // Joypad input
    device.memory[TIMA_REG] = 0x00; device.memory[TMA_REG] = 0x00; device.memory[TAC_REG] = 0x00;
    device.memory[0xFF10] = 0x80; device.memory[0xFF11] = 0xBF; device.memory[0xFF12] = 0xF3;
    device.memory[0xFF14] = 0xBF; device.memory[0xFF16] = 0x3F; device.memory[0xFF17] = 0x00;
    device.memory[0xFF19] = 0xBF; device.memory[0xFF1A] = 0x7F; device.memory[0xFF1B] = 0xFF;
    device.memory[0xFF1C] = 0x9F; device.memory[0xFF1E] = 0xBF; device.memory[0xFF20] = 0xFF;
    device.memory[0xFF21] = 0x00; device.memory[0xFF22] = 0x00; device.memory[0xFF23] = 0xBF;
    device.memory[0xFF24] = 0x77; device.memory[0xFF25] = 0xF3; device.memory[0xFF26] = 0xF1;
    device.memory[0xFF41] = 0x02; // STAT - Start in mode 2 (OAM scan)
    device.memory[0xFF42] = 0x00; // SCY
    device.memory[0xFF43] = 0x00; // SCX
    device.memory[0xFF44] = 0x00; // LY - will be updated by PPU
    device.memory[0xFF45] = 0x00; // LYC
    device.memory[0xFF47] = 0xE4; // BGP - Better palette: 11 10 01 00
    device.memory[0xFF48] = 0xFF; device.memory[0xFF49] = 0xFF;
    device.memory[0xFF4A] = 0x00; device.memory[0xFF4B] = 0x00;
    device.memory[IE_REG] = 0x00;
}

void InitializeBootROM() {
    FILE *bootROM = fopen("bootroms/dmg.bin", "rb");
    if(bootROM){
        fread(device.boot, 256, 1, bootROM);
        fclose(bootROM);
    }
}

    

void create_dummy_header() {
    // This is the correct, official Nintendo logo A
    uint8_t nintendo_logo[48] = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
        0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
        0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
    };

    // Copy the logo data into the correct memory location
    for (int i = 0; i < 48; ++i) {
        device.memory[0x0104 + i] = nintendo_logo[i];
    }

    // A valid header checksum. The boot ROM also verifies this.
    device.memory[0x014D] = 0xEA;
}

/* ==================== LOAD DYNAMIC STATE FROM A SAVED ONE =============================== */
/* This functionality is present for debugging purposes, loading a predetermined hardware 
 * state allow for easier tracing operation, to discover bugs and differences from well
 * known working emulators.
*/

static void skip_nl(uint8_t **hwsnap_buf, size_t *hwsnap_size) {
    bool c = true;
    while(c) {
        if(**hwsnap_buf == '\n') {
            advance(1);
            // For safe windows newlines
            if(**hwsnap_buf == '\r') {
                advance(1);
            }
        }
        else c = false;
    }
}
static bool match_cpu(uint8_t **hwsnap_buf, size_t *hwsnap_size) {
    if(memcmp(*hwsnap_buf, "CPU", 3) != 0) return false; 
    advance(3);
    skip_nl(hwsnap_buf, hwsnap_size);

    char z, n, h, c;
    int offset;
    if(sscanf(*(const char **)hwsnap_buf, "A:%hhx %n", (uint8_t*)&device.cpu.AF + 1, &offset) != 1) return false;
    advance(offset);

    if(sscanf(*(const char **)hwsnap_buf, "F:%c%c%c%c %n", &z, &n, &h, &c, &offset) != 4) return false; 
    advance(offset);

    if(z == 'Z') device.cpu.AF |= 0x80;
    else device.cpu.AF &= ~0x80;
    
    if(n == 'N') device.cpu.AF |= 0x40;
    else device.cpu.AF &= ~0x40;
    
    if(h == 'H') device.cpu.AF |= 0x20;
    else device.cpu.AF &= ~0x20;

    if(c == 'C') device.cpu.AF |= 0x10;
    else device.cpu.AF &= ~0x10;

    
    if(sscanf(*(const char **)hwsnap_buf, "BC:%hx DE:%hx HL:%hx SP:%hx PC:%hx%n", 
                &device.cpu.BC, 
                &device.cpu.DE, 
                &device.cpu.HL, 
                &device.cpu.SP, 
                &device.cpu.PC,
                &offset) != 5) return false;
   advance(offset);
   skip_nl(hwsnap_buf, hwsnap_size);

   device.cpu.halted = true; // Decide what to do here

   return true;
}

static bool match_ppu(uint8_t **hwsnap_buf, size_t *hwsnap_size) {
    if(memcmp(*hwsnap_buf, "PPU", 3) != 0) return false; 
    advance(3);
    skip_nl(hwsnap_buf, hwsnap_size);
    
    int ppu_mode, ly, offset;
    if(sscanf(*(const char **)hwsnap_buf, "mode:+%d LY:%u%n", &ppu_mode, &ly, &offset) != 2) return false; 
    ppu_set_mode(ppu_mode);
    ppu_set_ly(ly);

    advance(offset);
    skip_nl(hwsnap_buf, hwsnap_size);

    return true;
}

static bool match_timer(uint8_t **hwsnap_buf, size_t *hwsnap_size) {
    if(memcmp(*hwsnap_buf, "TIMER", 5) != 0) return false; 
    advance(5);
    skip_nl(hwsnap_buf, hwsnap_size);
    
    int offset;
    if(sscanf(*(const char **)hwsnap_buf, "div counter:%llu%n", &device.timer.div_cycle_counter,  &offset) != 1) return false; 
    advance(offset);
    skip_nl(hwsnap_buf, hwsnap_size);
    
    if(sscanf(*(const char **)hwsnap_buf, "tima counter:%llu%n", &device.timer.tima_cycle_counter, &offset) != 1) return false; 
    advance(offset);
    skip_nl(hwsnap_buf, hwsnap_size);

    return true;
}



static bool match_interrupts(uint8_t **hwsnap_buf, size_t *hwsnap_size) {
    if(memcmp(*hwsnap_buf, "INTERRUPTS", sizeof("INTERRUPTS") - 1) != 0) return false; 
    advance(sizeof("INTERRUPTS") - 1);
    skip_nl(hwsnap_buf, hwsnap_size);

    int offset;
    if(sscanf(*(const char **)hwsnap_buf, "ie:%hhx%n", &device.memory[IE_REG], &offset) != 2) return false; 
    advance(offset);
    skip_nl(hwsnap_buf, hwsnap_size);
    if(sscanf(*(const char **)hwsnap_buf, "if:%hhx%n", &device.memory[IF_REG], &offset) != 2) return false; 
    advance(offset);
    skip_nl(hwsnap_buf, hwsnap_size);
    if(sscanf(*(const char **)hwsnap_buf, "IME:%hhu%n", &device.cpu.IME, &offset) != 2) return false; 
    advance(offset);
    skip_nl(hwsnap_buf, hwsnap_size);

    return true;
}

static bool match_header(uint8_t **hwsnap_buf, size_t *hwsnap_size) {
    size_t hd_len = strlen(ST_FILE_HEADER);
    printf("%s\n", *(char **)hwsnap_buf);
    if(*hwsnap_size >= hd_len && memcmp(*hwsnap_buf, ST_FILE_HEADER, hd_len) == 0) {
        advance(hd_len);
        skip_nl(hwsnap_buf, hwsnap_size);
        return true;
    }
    return false;
}

static bool parse_hwsnap(uint8_t *hwsnap_buf, size_t hwsnap_size) {
    if(!match_header(&hwsnap_buf, &hwsnap_size)){
        printf("Error incorrect format for hw snapshot file\n");
        return false;
    }
    if(!match_cpu(&hwsnap_buf, &hwsnap_size)){
        printf("Error incorrect state file at: cpu\n");
        return false;
    }
    if(!match_ppu(&hwsnap_buf, &hwsnap_size)){
        printf("Error incorrect state file at: ppu\n");
        return false;
    }
    if(!match_interrupts(&hwsnap_buf, &hwsnap_size)){
        printf("Error incorrect state file at: interrupts\n");
        return false;
    }
        /*
    if(!match_timer(&hwsnap_buf, &hwsnap_size)){
        printf("Error incorrect state file at: timer\n");
        return;
    if(!match_dma(&hwsnap_buf, &hwsnap_size)){
        printf("Error incorrect state file at: dma\n");
        return;
    if(!match_cartridge(&hwsnap_buf, &hwsnap_size)){
        printf("Error incorrect state file at: cartridge\n");
        return;
    if(!match_memory(&hwsnap_buf, &hwsnap_size)){
        printf("Error incorrect state file at: memory\n");
        return;*/
    return true;
}

void InitializeHWSnapshot(const char *hwsnap_file_path){
    FILE *hwsnap_file = fopen(hwsnap_file_path, "r");
    if(!hwsnap_file) {
        printf("Error: status file not found!\n");
        return;
    }

    fseek(hwsnap_file, 0, SEEK_END);
    size_t hwsnap_size = ftell(hwsnap_file);
    fseek(hwsnap_file, 0, SEEK_SET);

    uint8_t *hwsnap_buf = malloc(hwsnap_size + 1);
    if(!hwsnap_buf) {
        printf("Buy more memory lol!\n");
        fclose(hwsnap_file);
        return;
    }

    fread(hwsnap_buf, hwsnap_size, 1, hwsnap_file);
    hwsnap_buf[hwsnap_size] = 0; // Null termination for safe sscanf 
    fclose(hwsnap_file);

    if(parse_hwsnap(hwsnap_buf, hwsnap_size)){
        printf("State file loaded succesfully\n");
    } 
    free(hwsnap_buf);
}
