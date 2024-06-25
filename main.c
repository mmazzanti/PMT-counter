#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pio_code.pio.h"

#define LED_PIN 20
int LED_STATE = 0;
float pi = 3.1415926;

int main() {
    
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    stdio_init_all();

    static const uint led_pin = 25;
    static const float pio_freq = 2000;

    // Choose PIO instance (0 or 1)
    PIO pio = pio0;

    // Get first free state machine in PIO 0
    uint sm = pio_claim_unused_sm(pio, true);

    // Add PIO program to PIO instruction memory. SDK will find location and
    // return with the memory offset of the program.
    uint offset = pio_add_program(pio, &blink_program);

    // Calculate the PIO clock divider
    float div = (float)clock_get_hz(clk_sys) / pio_freq;

    // Initialize the program using the helper function in our .pio file
    blink_program_init(pio, sm, offset, led_pin, div);

    // Start running our PIO program in the state machine
    pio_sm_set_enabled(pio, sm, true);

    // Do nothing
    while (true) {
        printf("Hello World From Pi Pico USB CDC\n");
        printf("Pi Value = %f\n", pi);
        sleep_ms(100);
    }
}