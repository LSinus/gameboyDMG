#include "timer.h"
#include "cpu.h"
#include "memory.h"

TIMER timer = {0};

/* This function updates timers for clock T-cycles executed */
void timer_step(int Tcycles){
    timer.div_cycle_counter   += Tcycles;
    timer.tima_cycle_counter += Tcycles;

    if(timer.div_cycle_counter >= (CLOCK_FREQ_HZ / DIV_INC_FREQ_HZ)){
        size_t increment = timer.div_cycle_counter / (CLOCK_FREQ_HZ / DIV_INC_FREQ_HZ);
        memory[DIV_REG] += increment;
        timer.div_cycle_counter %= (CLOCK_FREQ_HZ / DIV_INC_FREQ_HZ);
    }

    uint8_t TAC = memory[TAC_REG];
    if((TAC & 0x04) != 0){ // Enable = 1 so increment TIMA
        size_t tima_inc_rate;

        switch(TAC & 0x03){ // last 2 bits indicate the increment rate for TIMA
            case 0b00: tima_inc_rate = 4096;   break;
            case 0b01: tima_inc_rate = 262144; break;
            case 0b10: tima_inc_rate = 65536;  break;
            case 0b11: tima_inc_rate = 16384;  break;
        }
        uint32_t threshold = CLOCK_FREQ_HZ / tima_inc_rate;

        while (timer.tima_cycle_counter >= threshold) {
            timer.tima_cycle_counter -= threshold;

            // Increment TIMA by exactly 1
            memory[TIMA_REG]++;

            // If TIMA just overflowed (went from 255 to 0)
            if (memory[TIMA_REG] == 0) {
                // Load the value from TMA
                memory[TIMA_REG] = memory[TMA_REG];
                // Request a timer interrupt
                memory[IF_REG] |= 0x04;
            }
        }
    }
}

