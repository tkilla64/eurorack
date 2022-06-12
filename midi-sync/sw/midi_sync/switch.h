#include <stdio.h>
#include "pico/stdlib.h"

// Define IO pins
#define SW_MODE_0       12
#define SW_MODE_1       13
#define START_BUTTON_N  14
#define STOP_BUTTON_N   15

// operational modes
enum mode {
    NOT_USED = 0,
    EXT_SYNC,
    MIDI_SYNC,
    TAP_SYNC
};

// prototypes for switch.c
int read_mode_switch(void);
bool read_start_button_pressed(void);
bool read_start_button(void);
bool read_stop_button(void);
