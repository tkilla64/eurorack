/*
 * midi_sync (by Tommy Killander, tkilla64, MeeBilt):
 *
 * Clock generator and divider module with three clock sources:
 *  - MIDI System Real-time messages
 *  - External CLOCK and RESET source
 *  - Internal clock with Tap Tempo
 * Control buttons; START and PAUSE/STOP
 * LED display; BPM, MIDI PAUSED or TAP
 * Outputs; RESET, /1, /2, /4, /8 and /16
 * 
 * IDE: Visual Studio Code with Raspberry Pi Pico SDK installed. 
 * 
 * TODO:
 *  - Clock running during Tap Tempo capture
 * 
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "midi.h"
#include "display.h"
#include "switch.h"

// Common definitions
#define SW_VER_MAJOR    0U              // software version 
#define SW_VER_MINOR    11U

#define TICK_TIME_MS    4U
#define STARTSCREEN_MS  2000U           // no of ms to show sw ver
#define TAP_RESET_MS    1000U           // hold time for tap reset
#define BLINK_RATE_MS   1600U
#define BPM_AVG         100U            // average set size for ext and midi BPM calc
#define PPQ_24DIV4      (24U/4U)        // MIDI clock to 16 steps ratio
#define PPQ_24TOBPM     (60.0f/24.0f)   // MIDI clock to BPM ratio     
#define PPQ_4TOBPM      (60.0f/4.0f)    // ext clock to BPM ratio
#define TAP_LIMIT       8U              // number of taps needed to calculate tempo

#define ACTIVE          false
#define INACTIVE        true

// State-machine definitions
enum fsm {
    RUNNING = 0,
    PAUSE,
    RESTART,
    RESET,
    CAPTURE
} op_mode_t;

// Setup uart1 as MIDI port
#define UART_ID   uart1

// Define IO pins
#define CLOCK_IN_N      2
#define RESET_IN_N      3
#define UART_TX_PIN     4   // not used
#define UART_RX_PIN     5
#define RESET_OUT_N     6
#define DIG_100         7
#define DIG_010         8
#define DIG_001         9
#define START_LED_N     16
#define STOP_LED_N      17
#define DATA            18
#define LATCH           19
#define CLOCK           20

static int midi_srt_clock = 0;          
static int midi_running = false;        
static int midi_reset = false;
static uint64_t midi_ts_new_us;
static uint64_t midi_ts_old_us;
static int midi_clock_period_us = 0;

volatile bool timer_fired = false;
volatile long tick_counter = 0;
volatile long prec_counter = 0;
volatile int tap_periodtime_ms = 100;
volatile int tap_counter = 0;

static uint64_t ext_ts_new_us;
static uint64_t ext_ts_old_us;
static int ext_clock_period_us = 0;
static int ext_clock_pulses = 0;
volatile bool ext_clock_in_state;
volatile bool ext_reset_in_state = INACTIVE;
volatile bool reset_fired = false;

// Local functions
void init_gpio_pins(void);
void init_midi_port(void);

// GPIO edge triggered interrupt handler
// Note that ext_clock_pulses runs at double rate to give the correct clock output
void on_edge_event(uint gpio, uint32_t events) 
{
    if (gpio == CLOCK_IN_N)
    {
        ext_clock_in_state = gpio_get(CLOCK_IN_N);
        ext_clock_pulses++;
        if (events & GPIO_IRQ_EDGE_FALL)
        {
            ext_ts_new_us = time_us_64(); 
            ext_clock_period_us = ext_ts_new_us - ext_ts_old_us;
            ext_ts_old_us = ext_ts_new_us;
        }
    }
    if (gpio == RESET_IN_N)
    {
        ext_reset_in_state = gpio_get(RESET_IN_N);
        if (events & GPIO_IRQ_EDGE_FALL)
        {
            reset_fired = true;
        }
    }
}

// MIDI RX interrupt handler
void on_uart_rx() 
{
    while (uart_is_readable(UART_ID)) 
    {
        uint8_t ch = uart_getc(UART_ID);
        switch (ch)
        {
            case SRT_TIM_CLOCK:
                midi_srt_clock++;
                midi_ts_new_us = time_us_64(); 
                midi_clock_period_us = midi_ts_new_us - midi_ts_old_us;
                midi_ts_old_us = midi_ts_new_us;
                break;

            case SRT_START:
                midi_running = true;
                midi_reset = true;
                break;

            case SRT_CONT:
                midi_running = true;
                midi_reset = false;
                break;       
            
            case SRT_STOP:
                midi_running = false;
                midi_reset = false;
                break;

            default:
                break;
        }
    }
}

// Timer interrupt handlers
// 1 ms period for tap tempo clock. Tick based on TICK_TIME_MS.
bool on_repeating_timer_expired(struct repeating_timer *t) {
    if (++prec_counter % TICK_TIME_MS == 0)
    {
        display_increment_to_next_digit();
        tick_counter++;
        timer_fired = true;
    }
    if (prec_counter % tap_periodtime_ms == 0)
        tap_counter++;
    return true;
}

// Define which GPIOs that are used for turning on digits (LSD first)
int digit_gpios[NO_OF_DIGITS] =
{
    DIG_001,
    DIG_010,
    DIG_100
};

//
// Start of main loop
int main() 
{
    // common variables
    static long reset_time;
    volatile int operation_mode = 0;
    volatile uint16_t shiftdata;
    static uint8_t clock_out_n = 0;
    volatile bool databit;
    volatile bool blink;
    static bool start_button = 0;
    static bool stop_button = 0;

    // midi mode
    static int midi_state = PAUSE;
    static int midi_counter = 0;
    static int midi_last_bpm = 0;
    static float midi_accu_bpm = 0.0f;
    static bool midi_reset_out_n = INACTIVE;

    // ext clock mode
    static int ext_clock_state = PAUSE;
    static int ext_clk_counter = 0;
    static int ext_last_bpm = 0;
    static float ext_accu_bpm = 0.0f;

    // tap tempo mode
    static int tap_state = CAPTURE;
    static int tap_count = 0;
    static int tap_last_bpm = 0;
    static int tap_clock_period_us;
    static int tap_reset_timer = 0;
    static int tap_clk_counter = 0;
    static float tap_accu_bpm = 0.0f;
    static uint64_t tap_ts_new_us;
    static uint64_t tap_ts_old_us;
    static bool tap_reset_out_n = INACTIVE;

    init_gpio_pins();
    init_midi_port();
    
    // setup edge triggered interrupt handlers on CLOCK and RESET inputs
    gpio_set_irq_enabled_with_callback(CLOCK_IN_N, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &on_edge_event);
    gpio_set_irq_enabled(RESET_IN_N, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

    // setup periodic timer tick interrupt handler
    static struct repeating_timer timer;
    // Negative delay in ms means we will call repeating_timer_callback 
    // and call it again regardless of how long the callback took to execute
    add_repeating_timer_ms(-1, on_repeating_timer_expired, NULL, &timer);
    
    // Show SW version info
    display_sw_version(SW_VER_MAJOR, SW_VER_MINOR);

    midi_ts_old_us = time_us_64();

    // Main loop
    while (true)
    {
        // Service periodic time events
        if (timer_fired)
        {
            timer_fired = false;
            blink = (tick_counter%(BLINK_RATE_MS/TICK_TIME_MS)) / (BLINK_RATE_MS/TICK_TIME_MS/2);

            // Read status from Mode Select switch and buttons
            operation_mode = read_mode_switch();
            start_button = read_start_button();
            stop_button = read_stop_button();

            // Handle Ext Clock state
            if (operation_mode == EXT_SYNC)
            {
                switch (ext_clock_state)
                {
                    case RUNNING:
                        if (reset_fired)
                        {
                            ext_clock_pulses = 0;
                            reset_fired = false;
                        }
                        else
                            ext_clk_counter = ext_clock_pulses;
                        
                        if (stop_button)
                            ext_clock_state = PAUSE;
                        break;

                    case PAUSE:
                        if (start_button)
                            ext_clock_state = RUNNING;    

                        if (stop_button)
                        {
                            ext_clock_state = RESTART;
                            reset_time = tick_counter;
                        }
                        break;

                    case RESTART:
                        ext_reset_in_state = ACTIVE;
                        if (tick_counter == reset_time+2)
                            ext_clock_state = RESET;
                        break;

                    case RESET:
                        ext_clock_pulses = 0;
                        ext_reset_in_state = INACTIVE;
                        if (start_button)
                            ext_clock_state = RUNNING;
                        break;

                    default:
                        break;
                }
            }

            // Handle Tap-tempo mode
            if (operation_mode == TAP_SYNC)
            {
                switch(tap_state)
                {
                    case CAPTURE:
                        if (start_button)
                        {
                            if (tap_count == 0)
                            {
                                tap_ts_old_us = time_us_64();
                                tap_accu_bpm = 0;
                            }
                            else if (tap_count == TAP_LIMIT-1)
                            {
                                // calculate bpm
                                tap_state = RUNNING;
                                tap_last_bpm = (int)(tap_accu_bpm/(float)(TAP_LIMIT-2));
                                tap_count = 0;
                                tap_periodtime_ms = (int)(1000.0f/((float)tap_last_bpm/(PPQ_4TOBPM/2)));
                            }
                            else
                            {
                                // calculate time between taps
                                tap_ts_new_us = time_us_64(); 
                                tap_clock_period_us = tap_ts_new_us - tap_ts_old_us;
                                tap_ts_old_us = tap_ts_new_us;
                                tap_accu_bpm += (1000000.0f/(float)tap_clock_period_us)*60.0f;
                            }
                            tap_count++;
                        }
                        break;

                    case RUNNING:
                        tap_clk_counter = tap_counter;
                        if (read_start_button_pressed())
                        {
                            if (tap_reset_timer++ > TAP_RESET_MS/TICK_TIME_MS)
                            {
                                // tap reset
                                tap_state = CAPTURE;
                                tap_count = 0;
                                tap_reset_timer = 0;
                            }
                        }
                        if (stop_button)
                            tap_state = PAUSE;
                        break;

                    case PAUSE:
                        if (start_button)
                            tap_state = RUNNING;

                        if (stop_button)
                        {
                            tap_state = RESTART;
                            reset_time = tick_counter;
                        }
                        break;

                    case RESTART:
                        tap_reset_out_n = ACTIVE;
                        if (tick_counter == reset_time+2)
                            tap_state = RESET;
                        break;

                    case RESET:
                        tap_clk_counter = 0;
                        tap_counter = 0;
                        tap_reset_out_n = INACTIVE;
                        if (start_button)
                            tap_state = RUNNING;
                    default:
                        break;
                }
            }

            // Update display            
            // Shift out data into 16 bit shiftregister (MSB first)
            // nc nc nc clk1n clk2n clk4n clk8n clk16n dp g f e d c b a
            shiftdata = (clock_out_n << 8) | display_get_segment_data(display_get_current_digit());
            for (int i = 0 ; i < 16 ; i++)
            {
                gpio_put(CLOCK, false);
                databit = ((shiftdata << i) & 0x8000) == 0 ? false : true;
                gpio_put(DATA, databit);
                gpio_put(CLOCK, true);  
            }
            gpio_put(LATCH, true);
            gpio_put(CLOCK, false);            
            gpio_put(LATCH, false);

            // Set digit activation pin active
            for (int i = 0 ; i < NO_OF_DIGITS ; i++)
                if (i == display_get_current_digit())
                    gpio_put(digit_gpios[i], true);
                else
                    gpio_put(digit_gpios[i], false);

            // Control display and present operation state
            // skip if still in start screen (show sw version)
            if (tick_counter > (STARTSCREEN_MS/TICK_TIME_MS))
            {
                // Calculate MIDI BPM (MIDI SRT Clock frequency * 60) / 24
                midi_accu_bpm += (1000000.0f/(float)midi_clock_period_us)*PPQ_24TOBPM;
                if (tick_counter%BPM_AVG == 0)
                {
                    midi_last_bpm = (int)((midi_accu_bpm/(float)BPM_AVG)+0.5f);
                    midi_accu_bpm = 0;
                }
                // Calculate Ext Clock BPM (4PPQ Clock frequency * 60) / 4
                ext_accu_bpm += (1000000.0f/(float)ext_clock_period_us)*PPQ_4TOBPM;
                if (tick_counter%BPM_AVG == 0)
                {
                    ext_last_bpm = (int)((ext_accu_bpm/(float)BPM_AVG)+0.5f);
                    ext_accu_bpm = 0;
                }

                // Output result to display
                switch (operation_mode)
                {
                    case MIDI_SYNC:
                        gpio_put(START_LED_N, INACTIVE);
                        gpio_put(STOP_LED_N, INACTIVE);
                        if (midi_state == RUNNING)
                        {                            
                            display_unsigned_value(midi_last_bpm);
                        }
                        else if (midi_state == PAUSE)
                        {
                            display_digit_char(2, blink ? 17 : 16);
                            display_digit_char(1, 20);
                            display_digit_char(0, blink ? 17 : 16);
                        }
                        break;
               
                    case EXT_SYNC:
                        display_unsigned_value(ext_last_bpm);
                        if (ext_clock_state == RUNNING)
                        {                            
                            gpio_put(START_LED_N, ACTIVE);
                            gpio_put(STOP_LED_N, INACTIVE);
                        }
                        else if (ext_clock_state == PAUSE)
                        {      
                            gpio_put(START_LED_N, INACTIVE);
                            gpio_put(STOP_LED_N, blink);
                        }
                        else 
                        {
                            gpio_put(START_LED_N, INACTIVE);
                            gpio_put(STOP_LED_N, ACTIVE);                            
                        }
                        break;

                    case TAP_SYNC:
                        if (tap_state == CAPTURE) 
                        {
                            gpio_put(START_LED_N, INACTIVE);
                            gpio_put(STOP_LED_N, INACTIVE);
                            display_digit_char(2, 21);
                            display_digit_char(1, 10);
                            display_digit_char(0, 22);
                        }
                        else if (tap_state == RUNNING)
                        {
                            display_unsigned_value(tap_last_bpm);
                            gpio_put(START_LED_N, ACTIVE);
                            gpio_put(STOP_LED_N, INACTIVE);
                        }
                        else if (tap_state == PAUSE)
                        {
                            display_unsigned_value(tap_last_bpm);
                            gpio_put(START_LED_N, INACTIVE);
                            gpio_put(STOP_LED_N, blink);
                        }
                        else 
                        {
                            gpio_put(START_LED_N, INACTIVE);
                            gpio_put(STOP_LED_N, ACTIVE);                            
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        // Handle MIDI state machine
        // 24 PPQ (pulses per quarternote): 6 clocks per 16 steps/bar.
        // Note that midi_counter runs at double rate (PPQ_24DIV4/2) to give correct clock output
        switch (midi_state)
        {
            case RUNNING:   // Running mode where we output clocks until we receive
                            // MIDI Stop or Start events
                if (midi_running)
                        midi_counter = midi_srt_clock/(PPQ_24DIV4/2); 
                else 
                    if (midi_reset)
                        midi_state = RESTART;
                    else
                        midi_state = PAUSE;
                break;

            case PAUSE:     // We have received a Midi Stop Event, so we stay here
                            // until we have received Start or Continue event
                if (midi_reset) 
                {
                    midi_state = RESTART;
                    reset_time = tick_counter;
                }
                else
                    if (midi_running)
                        midi_state = RUNNING;
                break;

            case RESTART:   // We have received a MIDI Start event so we should reset
                            // to start of pattern/song
                midi_reset_out_n = ACTIVE;
                if (tick_counter == reset_time+2)
                    midi_state = RESET;
                break;

            case RESET:     // RESET output is held active during a clock phase
                            // and after that we go back to running
                midi_srt_clock = 0;
                midi_reset = false;
                midi_reset_out_n = INACTIVE;
                midi_state = RUNNING;
                break;

            default:
                break;
        }

        // Select RESET and CLOCK sources and 
        // control outputs depending on op mode
        switch (operation_mode)
        {
            case MIDI_SYNC:
                gpio_put(RESET_OUT_N, midi_reset_out_n);
                clock_out_n = (uint8_t)(midi_counter & 0b00011111);
                break;
               
            case EXT_SYNC:
                if (ext_clock_state != PAUSE)
                    gpio_put(RESET_OUT_N, ext_reset_in_state);     
                clock_out_n = (uint8_t)(ext_clk_counter & 0b00011111);
                break;

            case TAP_SYNC:
                gpio_put(RESET_OUT_N, tap_reset_out_n);
                clock_out_n = (uint8_t)(tap_clk_counter & 0b00011111);
            default:
                break;
        }
    }
}

// setup GPIO pin modes
void init_gpio_pins(void)
{
    // select GPIO mode
    gpio_init(CLOCK_IN_N);
    gpio_init(RESET_IN_N);
    gpio_init(RESET_OUT_N);
    gpio_init(DIG_100);
    gpio_init(DIG_010);
    gpio_init(DIG_001);
    gpio_init(SW_MODE_0);
    gpio_init(SW_MODE_1);
    gpio_init(START_BUTTON_N);
    gpio_init(STOP_BUTTON_N);
    gpio_init(START_LED_N);
    gpio_init(STOP_LED_N);
    gpio_init(DATA);
    gpio_init(LATCH);
    gpio_init(CLOCK);

    // setup input pins
    gpio_set_dir(CLOCK_IN_N, GPIO_IN);
    gpio_set_dir(RESET_IN_N, GPIO_IN);
    gpio_set_dir(SW_MODE_0, GPIO_IN);
    gpio_set_dir(SW_MODE_1, GPIO_IN);
    gpio_set_dir(START_BUTTON_N, GPIO_IN);
    gpio_set_dir(STOP_BUTTON_N, GPIO_IN);
    gpio_disable_pulls(CLOCK_IN_N);
    gpio_disable_pulls(RESET_IN_N);
    gpio_disable_pulls(SW_MODE_0);
    gpio_disable_pulls(SW_MODE_1);
    gpio_disable_pulls(START_BUTTON_N);
    gpio_disable_pulls(STOP_BUTTON_N);

    // setup output pins
    gpio_set_dir(RESET_OUT_N, GPIO_OUT);
    gpio_set_dir(DIG_100, GPIO_OUT);
    gpio_set_dir(DIG_010, GPIO_OUT);
    gpio_set_dir(DIG_001, GPIO_OUT);
    gpio_set_dir(START_LED_N, GPIO_OUT);
    gpio_set_dir(STOP_LED_N, GPIO_OUT);
    gpio_set_dir(DATA, GPIO_OUT);
    gpio_set_dir(LATCH, GPIO_OUT);
    gpio_set_dir(CLOCK, GPIO_OUT);

    gpio_put(RESET_OUT_N, true);
    gpio_put(START_LED_N, true);
    gpio_put(STOP_LED_N, true);
    gpio_put(LATCH, false);
} 

void init_midi_port(void)
{
    // Set up our UART with a basic baud rate.
    uart_init(UART_ID, 2400);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // Actually, we want a different speed
    // The call will return the actual baud rate selected, which will be as close as
    // possible to that requested
    int __unused actual = uart_set_baudrate(UART_ID, BAUD_RATE);

    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_hw_flow(UART_ID, false, false);

    // Set our data format
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);

    // Turn off FIFO's - we want to do this character by character
    uart_set_fifo_enabled(UART_ID, false);

    // Set up a RX interrupt
    // We need to set up the handler first
    // Select correct interrupt for the UART we are using
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

    // And set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(UART_ID, true, false);
}
