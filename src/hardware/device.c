#include "device.h"
#include "debugger.h"

DEVICE device = {0};


/* Copies the status of the emulator inside the buffer, the provided buffer 
   must be at least 82 bytes
*/
void GetEmulatorStatus(char* buf){
    uint8_t a =  device.cpu.AF >> 8;
    uint8_t f = (device.cpu.AF & 0xFF);
    uint8_t b =  device.cpu.BC >> 8;
    uint8_t c = (device.cpu.BC & 0xFF);
    uint8_t d =  device.cpu.DE >> 8;
    uint8_t e = (device.cpu.DE & 0xFF);
    uint8_t h =  device.cpu.HL >> 8;
    uint8_t l = (device.cpu.HL & 0xFF);

    char formatted_f_reg[5] = {0};
    for(int i = 0; i<4; i++){
        formatted_f_reg[i] = '-';
    }

    if(f & 0b1000) formatted_f_reg[0] = 'Z';
    if(f & 0b0100) formatted_f_reg[1] = 'N';
    if(f & 0b0010) formatted_f_reg[2] = 'H';
    if(f & 0b0001) formatted_f_reg[3] = 'C';

    sprintf(buf, "A 0x%02x F 0x%02x B 0x%02X C: 0x%02X D: 0x%02X E: 0x%02X H: 0x%02X L: 0x%02X\nSP: 0x%04X PC: 0x%04X\n(%02X %02X %02X %02X)\n Halted: %d    Stopped: %d", a, f, b, c, d, e, h, l, device.cpu.SP, device.cpu.PC, ReadMem(device.cpu.PC), ReadMem(device.cpu.PC+1), ReadMem(device.cpu.PC+2), ReadMem(device.cpu.PC+3), device.cpu.halted, !device.cpu.running);
}

void GetEmulatorStatusFile(char* buf){
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
    sprintf(buf, "A:%02x F:%s BC:%04x DE:%04x HL:%04x SP:%04x PC:%04x ppu:%c%u LY:%u |%s\n", a, formatted_f_reg, device.cpu.BC, device.cpu.DE, device.cpu.HL, device.cpu.SP, device.cpu.PC, enable, ppu_get_mode(), device.ppu.ly, tmp);
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
        device.cpu.PC = 0x0060;
    }

    return 20;
}

void InitializePowerOnState(){
    device.cpu.PC = 0x0000;
    device.cpu.SP = 0x0000;
    device.cpu.AF = 0x0000;
    device.cpu.BC = 0x0000;
    device.cpu.DE = 0x0000;
    device.cpu.HL = 0x0000;
    
    device.cpu.halted = false;
    device.cpu.running = true;
    device.cpu.IME = false;

    device.boot_rom_enabled = true;

    // Initialize PPU state properly
    device.ppu.mode = MODE_2_OAM_SCAN;
    device.ppu.cycle_counter = 0;
    device.ppu.ly = 0;

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
    FILE *bootROM = fopen("gb-bootroms/bin/dmg.bin", "rb");
    if(bootROM){
        fread(device.boot, 256, 1, bootROM);
        fclose(bootROM);
    }
}

bool InitializeGameROM(char *romPath) {
    FILE *program = fopen(romPath, "rb");
    size_t program_length;
    if(program){
        fseek(program, 0, SEEK_END);
        program_length = ftell(program);
        fseek(program, 0, SEEK_SET);
        fread(device.memory, program_length, 1, program);
        fclose(program);

        device.cartridge.title = (char *)(&device.memory[0x0134]); 
        printf("Cartridge title: %s\n", device.cartridge.title);
        device.cartridge.type = cartridge_types[device.memory[0x0147]];
        if (device.cartridge.title) {
            printf("Cartridge type: %s\n", device.cartridge.type);
        } else {
            printf("Cartridge type: Unknown (0x%02X)\n", device.memory[0x0147]);
        }

        device.romPath = romPath;
        return true;
    }
    return false;
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
