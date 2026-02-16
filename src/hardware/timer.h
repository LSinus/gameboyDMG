#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define DIV_INC_FREQ_HZ 16384

/* Definition of Timer state machine */
typedef struct TIMER {
    uint64_t div_cycle_counter;
    uint64_t tima_cycle_counter;
} TIMER;

void timer_step(int Tcycles);

#define set_div_counter(cycles) \
    do{\
        uint64_t n = (uint64_t)cycles;\
        device.timer.div_cycle_counter = cycles;\
    } while(0)


#endif
