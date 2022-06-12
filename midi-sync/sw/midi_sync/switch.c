//
// Functions for handling switches and buttons

#include <stdio.h>
#include "switch.h"

bool debounce_button(int index, int inputpin);

// Return the value of mode toggle switch
int read_mode_switch(void)
{
    int mode_0 = (int)gpio_get(SW_MODE_0);
    int mode_1 = ((int)gpio_get(SW_MODE_1) << 1);
    return (mode_0+mode_1);
}

// Return state of START button
bool read_start_button(void)
{
    return debounce_button(0, START_BUTTON_N);
}

// Return state of STOP button
bool read_stop_button(void)
{
    return debounce_button(1, STOP_BUTTON_N);
}

// return true if START button is held down
bool read_start_button_pressed(void)
{
    return (!gpio_get(START_BUTTON_N));
}

// Debounce (active low) button, Jack Ganssle style
bool debounce_button(int index, int inputpin) 
{
  static uint16_t state[2] = { 0, 0 };
  state[index] = (state[index]<<1) | gpio_get(inputpin) | 0xfe00;
  return (bool)(state[index] == 0xff00);
}