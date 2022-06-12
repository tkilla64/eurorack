// Defines for MIDI

// 5-pin DIN (or TRS) communication parameters
#define BAUD_RATE 31250
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

// System Real-Time messages
#define SRT_TIM_CLOCK   0xF8
#define SRT_START       0xFA
#define SRT_CONT        0xFB
#define SRT_STOP        0xFC
#define SRT_ACT_SENS    0xFE
#define SRT_SYS_RESET   0xFF
