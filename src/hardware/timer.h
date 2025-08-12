#ifndef TIMER_H
#define TIMER_H

#include <stdio.h>

#define DIV_INC_FREQ_HZ 16384

/* Definition of Timer state machine */
typedef struct TIMER {
    size_t div_cycle_counter;
    size_t tima_cycle_counter;
} TIMER;

extern TIMER timer;


void timer_step(int Tcycles);


#endif