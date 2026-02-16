#ifndef GUI_HOTRELOAD
#define GUI_HOTRELOAD

#include "gui.h"

#ifdef DEBUGGER_HOTRELOAD
    #define FUNC(name, ...) extern name##_t *name;
    LIST_OF_FUNCS
    #undef FUNC
    bool reload_libgui(void);

#else
    #define FUNC(name, ...) name##_t name;
    LIST_OF_FUNCS
    #undef FUNC
    #define reload_libgui() true

#endif //DEBUGGER_HOTRELOAD
       
#endif //GUI_HOTRELOAD
