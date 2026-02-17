#ifndef GUI_H
#define GUI_H

#include "../../external/microui.h"
#include "renderer.h"
#include "../hardware/device.h"
#include <SDL2/SDL.h>

typedef struct {
    // Hardware access
    uint8_t (*read_mem)(uint16_t addr);
    void    (*write_mem)(uint16_t addr, uint8_t val);
    
    // Emulator control
    void    (*get_emulator_status)(char* buffer, size_t size);
    void    (*restart_emulator)(void);
    void    (*init_boot_rom)(void);
    bool    (*init_cartridge)(char *romPath);
    bool    (*shutdown_cartridge)(void);
    void    (*print_cartridge_info)(char *buf, size_t size);
    
} GuiExternalInterface;

#ifdef DEBUGGER_MODE
#define LIST_OF_FUNCS \
    FUNC(gui_init, void, const char *, int, int, const char*, DEVICE *, GuiExternalInterface *, SDL_mutex *) \
    FUNC(gui_process_event, void, SDL_Event *) \
    FUNC(gui_pre_reload, void*, void) \
    FUNC(gui_post_reload, void, void *) \
    FUNC(gui_render, void, void) \
    FUNC(gui_process_framebuffer, void, int, int, uint8_t) \
    FUNC(gui_process_tiledata, void, ) \
    FUNC(gui_quit, void, void) 

#else
#define LIST_OF_FUNCS \
    FUNC(gui_init, void, const char *, int, int, const char*, DEVICE *, GuiExternalInterface *, SDL_mutex *) \
    FUNC(gui_render, void, void) \
    FUNC(gui_process_framebuffer, void, int, int, uint8_t) \
    FUNC(gui_quit, void, void) 

#endif //DEBUGGER_MODE

#define FUNC(name, ret, ...) typedef ret (name##_t)(__VA_ARGS__);
LIST_OF_FUNCS
#undef FUNC 

#endif //GUI_H
