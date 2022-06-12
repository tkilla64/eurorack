// 
// Functions for handling a 7-segment display consisting of one or more digits

#include <stdio.h>
#include "display.h"

// Segment layout
//
//       ---A---
//      |       |
//      F       B
//      |       |
//       ---G---
//      |       |
//      E       C
//      |       |
//       ---D---  o DP

#define MASK_SEG_DP     0b10000000
#define MASK_SEG_G      0b01000000
#define MASK_SEG_F      0b00100000    
#define MASK_SEG_E      0b00010000
#define MASK_SEG_D      0b00001000    
#define MASK_SEG_C      0b00000100
#define MASK_SEG_B      0b00000010    
#define MASK_SEG_A      0b00000001

static const uint8_t segment_value[] = 
{ 
    MASK_SEG_A+MASK_SEG_B+MASK_SEG_C+MASK_SEG_D+
    MASK_SEG_E+MASK_SEG_F,                          // 0
    MASK_SEG_B+MASK_SEG_C,                          // 1
    MASK_SEG_A+MASK_SEG_B+MASK_SEG_D+MASK_SEG_E+
    MASK_SEG_G,                                     // 2
    MASK_SEG_A+MASK_SEG_B+MASK_SEG_C+MASK_SEG_D+
    MASK_SEG_G,                                     // 3
    MASK_SEG_B+MASK_SEG_C+MASK_SEG_F+MASK_SEG_G,    // 4
    MASK_SEG_A+MASK_SEG_C+MASK_SEG_D+MASK_SEG_F+
    MASK_SEG_G,                                     // 5
    MASK_SEG_A+MASK_SEG_C+MASK_SEG_D+MASK_SEG_E+
    MASK_SEG_F+MASK_SEG_G,                          // 6
    MASK_SEG_A+MASK_SEG_B+MASK_SEG_C,               // 7
    MASK_SEG_A+MASK_SEG_B+MASK_SEG_C+MASK_SEG_D+
    MASK_SEG_E+MASK_SEG_F+MASK_SEG_G,               // 8   
    MASK_SEG_A+MASK_SEG_B+MASK_SEG_C+MASK_SEG_D+
    MASK_SEG_F+MASK_SEG_G,                          // 9
    MASK_SEG_A+MASK_SEG_B+MASK_SEG_C+MASK_SEG_E+
    MASK_SEG_F+MASK_SEG_G,                          // A
    MASK_SEG_C+MASK_SEG_D+MASK_SEG_E+MASK_SEG_F+
    MASK_SEG_G,                                     // B
    MASK_SEG_D+MASK_SEG_E+MASK_SEG_G,               // C
    MASK_SEG_B+MASK_SEG_C+MASK_SEG_D+MASK_SEG_E+
    MASK_SEG_G,                                     // D
    MASK_SEG_A+MASK_SEG_D+MASK_SEG_E+MASK_SEG_F+
    MASK_SEG_G,                                     // E
    MASK_SEG_A+MASK_SEG_E+MASK_SEG_F+MASK_SEG_G,    // F
    0,                                              // blank
    MASK_SEG_G,                                     // dash
    MASK_SEG_D,                                     // underscore
    MASK_SEG_A,                                     // overscore
    MASK_SEG_B+MASK_SEG_C+MASK_SEG_E+MASK_SEG_F,    // double-bar
    MASK_SEG_D+MASK_SEG_E+MASK_SEG_F+MASK_SEG_G,    // t
    MASK_SEG_A+MASK_SEG_B+MASK_SEG_E+MASK_SEG_F+
    MASK_SEG_G,                                     // P
};

volatile int disp_digit;                          // current digit that is showing
volatile uint8_t display_output[NO_OF_DIGITS];    // pos [0] holds LSD, [NO_OF_SEGMENTS-1] holds MSD

//
// API

// Display major(X) and minor(YY) SW version in format X.YY
void display_sw_version(int major, int minor)
{   
    display_digit_decimal(NO_OF_DIGITS-1, major);
    display_decimalpoint(NO_OF_DIGITS-1, true);
    display_digit_decimal(NO_OF_DIGITS-2, minor / 10);
    display_digit_decimal(NO_OF_DIGITS-3, minor % 10);
}

// Clear display 
void display_clear(void)
{
    for (int i = 0 ; i < NO_OF_DIGITS ; i++)
        display_output[i] = 0;
}

// Increment to the next segment
void display_increment_to_next_digit(void)
{
    disp_digit = ++disp_digit % NO_OF_DIGITS;
}

// Return which digit is currently showing 
int display_get_current_digit(void)
{
    return disp_digit;
}

// Return the segment data from a specific digit
uint8_t display_get_segment_data(int digit)
{
    return display_output[digit];
}

// Set or clear decimal point for a specific digit
void display_decimalpoint(int digit, bool value)
{
    if (value)
        display_output[digit] |= MASK_SEG_DP;
    else
        display_output[digit] &= ~MASK_SEG_DP;
}

// Set a decimal value to a specific digit (0-9)
void display_digit_decimal(int digit, int value)
{
    if (value >= 0 && value < 10)
        display_output[digit] = segment_value[value];
}

// Set a char to a specific digit
void display_digit_char(int digit, int value)
{
    if (value >= 0 && value < sizeof(segment_value)+1)
        display_output[digit] = segment_value[value];
}

// Display an integer value in the range 000 to 999
void display_unsigned_value(int value)
{
    if (value >= 0 && value < 1000)
    {
        display_digit_decimal(NO_OF_DIGITS-1, value/100);
        display_digit_decimal(NO_OF_DIGITS-2, (value/10)%10);
        display_digit_decimal(NO_OF_DIGITS-3, value%10);
    }
}