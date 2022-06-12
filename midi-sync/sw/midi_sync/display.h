#include <stdio.h>
#include "pico/stdlib.h"

#define NO_OF_DIGITS  3

// Prototypes for display.c
void display_clear(void);
void display_sw_version(int major, int minor);
int display_get_current_digit(void);
void display_increment_to_next_digit(void);
uint8_t display_get_segment_data(int segment);
void display_decimalpoint(int digit, bool value);
void display_digit_decimal(int digit, int value);
void display_digit_char(int digit, int value);
void display_unsigned_value(int value);
