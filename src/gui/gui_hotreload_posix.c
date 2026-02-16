#ifdef DEBUGGER_HOTRELOAD
#include "gui_hotreload.h"
#include <stdio.h>
#include <dlfcn.h>

static const char *libgui_file_name = "libgui.so";

static void* libgui = NULL;

#define FUNC(name, ...) name##_t *name = NULL;
LIST_OF_FUNCS
#undef FUNC

bool reload_libgui() {
    if(libgui != NULL) dlclose(libgui);

    libgui = dlopen(libgui_file_name, RTLD_NOW);
    if(libgui == NULL) {
        fprintf(stderr, "[GUI HOTRELOAD] Error: could not load %s: %s", libgui_file_name, dlerror());
        return false;
    }

    #define FUNC(name, ...)\
        name = dlsym(libgui, #name); \
        if(name == NULL) {\
            fprintf(stderr, "[GUI HOTRELOAD] Error: could not find %s symbol in %s: %s",\
                    #name, libgui_file_name, dlerror());\
            return false;\
        }
    LIST_OF_FUNCS
    #undef FUNC

    return true;
}



#endif //DEBUGGER_HOTRELOAD
