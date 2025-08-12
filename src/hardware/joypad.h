#ifndef JOYPAD_H
#define JOYPAD_H

#include <stdbool.h>

/* JoyPad state struct */
typedef struct JOYPAD {
    bool start, select, b, a;
    bool down, up, left, right;
} JOYPAD;

extern JOYPAD joypad;

#endif