#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

//Speedup USB data retrieval
//#include "tusb.h"
//#include "bsp/board.h"

// PIO and DMA
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pio_code.pio.h"
#include "hardware/dma.h"
#include "hardware/structs/bus_ctrl.h"

// PINCRTL_IN_BASE = 0 (bit 0 -> GIPO 0, bit 1 -> GPIO 1, ... bit 7 -> GPIO 7)
#define IN_PIN_0 0
#define CAPTURE_PIN_COUNT 8

#define SHIFT_REG_WIDTH 32

// Latch Inputs (outputs for RP2040)
#define OE 10
#define LE 11 // Technically called CLK in the datasheet, but LE is more descriptive and cannot be confused with the pulses clock

// Counter Inputs (outputs for RP2040)
#define CE 12
#define TLCD 13
#define PE 14
#define MR 15

// Counter Outpus (inputs for RP2040)
#define COUT 8
#define TC 9

// Trigger Input used for triggering from the RP2040
#define TRIG 16

#define MAX_MEMORY 249836
#define MAX_COMMAND_LENGTH 6

char cmd[MAX_COMMAND_LENGTH+1];

uint N_SAMPLES = 0;
uint EXP_TIME = 0;

void init_pins()
{
    // Set the direction of the pins

    gpio_init(OE);
    gpio_set_dir(OE, GPIO_OUT);
    gpio_put(OE, 0); // Set latch output in high impedance state at booting
    gpio_init(LE);
    gpio_set_dir(LE, GPIO_OUT);
    gpio_init(CE);
    gpio_set_dir(CE, GPIO_OUT);
    gpio_init(TLCD);
    gpio_set_dir(TLCD, GPIO_OUT);
    gpio_init(PE);
    gpio_set_dir(PE, GPIO_OUT);
    gpio_init(MR);
    gpio_set_dir(MR, GPIO_OUT);
    gpio_init(TRIG);
    gpio_set_dir(TRIG, GPIO_IN);
    for (int i = 0; i < 8; i++)
    {
        gpio_init(IN_PIN_0 + i);
        gpio_set_dir(IN_PIN_0 + i, GPIO_IN);
    }
    sleep_ms(1000);  // After 1s the board is ready
    gpio_put(OE, 0); // Remove high impedance state from latch outputs
}

void print_capture_buf(const uint32_t *buf, uint32_t n_samples, uint32_t n_pins, size_t buf_size_bytes)
{
    printf("Acquired Data:\n");
    // Store data in data array (each 32 bits we have n_pins words of data)
    uint32_t num_words = 32 / n_pins;
    printf("Buffer length: %d\r\n", buf_size_bytes);
    printf("---- DATA ----\r\n");
    //printf("%.*s", buf_size_bytes, buf);
    //printf("Buffer size: %d, Buffer address: %x", buf_size_bytes, buf);
    fflush(stdout);
    int outcoume = fwrite(buf, sizeof(uint32_t), buf_size_bytes, stdout);
    printf("Written: %x", outcoume);
    printf("---- END DATA ----\r\n");
    fflush(stdout);
    // Old (slow) cout
    //
    /*
    for (int sample = 0; sample < n_samples; ++sample)
    {
        for (int i = 0; i < num_words; i++)
        {
            if (i == 0 && sample == 0)
            {
                // Skip first count as it was the previous data (we read the data while integrating the counts)
                continue;
            }
            printf("%d, ",(buf[sample] >> (n_pins * (i))) & 0xff);

        }
    }*/
}

void start_PMT_counter(PIO pio, uint sm, uint dma_chan, uint32_t *capture_buf, size_t capture_size_words, uint trigger_pin, bool trigger_level)
{

    pio_sm_set_enabled(pio, sm, false);
    // Need to clear _input shift counter_, as well as FIFO, because there may be
    // partial ISR contents left over from a previous run. sm_restart does this.

    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);

    // Configure the DMA to shift data from the PIO's RX FIFO to our buffer
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
    printf("Capture words: %d\r\n", capture_size_words);
    // Configure the DMA channel
    dma_channel_configure(dma_chan, &c,
                          capture_buf,        // Destination pointer
                          &pio->rxf[sm],      // Source pointer
                          capture_size_words, // Number of transfers
                          true                // Start immediately
    );

    pio_sm_set_enabled(pio, sm, true);
}

// From RP docs, use this function to determine the size of the allocated memory in case of Npins !=8
static inline uint bits_packed_per_word(uint pin_count)
{
    // If the number of pins to be sampled divides the shift register size, we
    // can use the full SR and FIFO width, and push when the input shift count
    // exactly reaches 32. If not, we have to push earlier, so we use the FIFO
    // a little less efficiently.
    return SHIFT_REG_WIDTH - (SHIFT_REG_WIDTH % pin_count);
}

bool check_binning_range(int val){
    if (!(val > 0 && val <= MAX_MEMORY))
        {
            printf("ERR: The number of bins is not correct, do not exceed memory limits of %d\r\n", MAX_MEMORY);
            return false;
        }
    return true;

}

bool check_exp_time(int val){
    if (!(val >= 0 && val <= 32))
        {
            printf("ERR: Invalid bin time\r\n");
            return false;
        }
    return true;
}

bool process_cmd(uint input_case) {
    uint val;
    
    switch (input_case) {
    case 0:
        if (0 == strncmp(cmd, "INIT", 4)) {
            return true;
        } else {
            printf("ERR: Unknown command\r\n");
            return false;
        }
        break;
    case 1:
        // Getting N_SAMPLES
        val = strtol(cmd, NULL, 10);
        if (check_binning_range(val))
        {
            return false;
        }
        N_SAMPLES = val;
        return true;
    case 2:
        // Getting EXP_TIME
        val = strtol(cmd, NULL, 10);
        if (check_exp_time(val))
        {
            return false;
        }
        EXP_TIME = val;
        return true;
    case 3:
        if (0 == strncmp(cmd, "SET ", 4)) {
            int tmp;
            char* rest = cmd;
            strtok_r(rest, " ", &rest);
            tmp = atoi(strtok_r(rest, " ", &rest));
            if (!check_binning_range(tmp))
            {
                return false;
            }
            N_SAMPLES = tmp;
            printf("NSAMPLES: %d", tmp);
            tmp = atoi(strtok_r(rest, " ", &rest));
            if (!check_exp_time(tmp))
            {
                return false;
            }
            EXP_TIME = tmp;
            printf("EXPTIME: %d", tmp); 
            return true;
        } else {
            printf("ERR: Unknown command\r\n");
            return false;
        }
        break;
    default:
        printf("Unknown command\r\n");
        return false;
    }
}


void readCmd(void) {
    uint idx = 0;
    while (true) {
        char c = getchar();
        if (c == '\b' || c == (char)127) {
            /* Backspace was pressed.  Erase the last character
             in the string - if any. */
            if (idx > 0) {
                idx--;
                cmd[idx] = '\0';
                printf("\b \b");
            }
        }
        if (c == 0x0D) {
            cmd[idx] = 0;
            printf("\r\n");
            return;
        }
        if ((c >= 0x20) && (c <= 0x7E)) {
            printf("%c", c);
            if (idx < 256) {
                cmd[idx++] = c;
            }
        }
    }
}

void read_input_config(uint *N_SAMPLES, uint *EXP_TIME) {
    //printf("Type INIT to start:\n");
    readCmd();
    if(process_cmd(3)){ //Speed command, set both N_SAMPLES and EXP_TIME
        return;
    }
    while (!process_cmd(0)){
        // Might be a SET command
        if (process_cmd(3)){
            return;
        }
        printf("Type INIT or SET to start:\r\n");
        readCmd();
    }
    printf("N of samples:\r\n");
    readCmd();
    while(!process_cmd(1)){
        printf("N of samples:\n");
        readCmd();
    }
    printf("Bin time (units of 1/125MHz) [0-32]:\r\n");
    readCmd();
    while(!process_cmd(2)){
        printf("Bin time (units of 1/125MHz) [0-32]:\r\n");
        readCmd();
    }

}

int main()
{
    // Init all GPIO ports needed (not sure this part of the code is needed...)
    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);
    init_pins();

    //Init TinyUSB
    tusb_init();
    

    printf("--- PMT counter ---\r\n");

    PIO pio = pio0;
    uint sm = 0;
    uint dma_chan = 0;

    uint test = 0;

    uint offset = pio_add_program(pio, &PMTcounter_program);

    // DEBUG: Slow down board for debugging
    //float div = (float)clock_get_hz(clk_sys) / 1000000;

    // Beginning of the experiment
    while (true)
    {
        //int dma_chan = dma_claim_unused_channel(true);
        // Read user settings (EXP_TIME atm not used)
        read_input_config(&N_SAMPLES, &EXP_TIME);

        printf("N_SAMPLES = %d\r\n", N_SAMPLES);
        printf("EXP_TIME = %d\r\n", EXP_TIME);

        printf("Allocating memory\r\n");
        // Allocate memory for the capture buffer
        uint total_sample_bits = N_SAMPLES * CAPTURE_PIN_COUNT;
        total_sample_bits += SHIFT_REG_WIDTH - 1;
        uint buf_size_words = total_sample_bits / SHIFT_REG_WIDTH;
        uint32_t *capture_buf = malloc(buf_size_words * sizeof(uint32_t));
        hard_assert(capture_buf);
        // Clear the buffer
        for (int i = 0; i < buf_size_words; i++)
        {
            capture_buf[i] = 0;
        }

        if (capture_buf == NULL)
        {
            printf("ERR: Error in memory allocation, restart the device and report the issue.\r\n");
            exit(0);
        }
        printf("Set DMA priority\r\n");
        // Grant high bus priority to the DMA, so it can shove the processors out
        // of the way. This should only be needed if you are pushing things up to
        // >16bits/clk here, i.e. if you need to saturate the bus completely.
        bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
        // Use PIO 0 SM 0
        PMTcounter_program_init(pio, sm, offset, IN_PIN_0, 9, OE, 6, LE, 5, MR, SHIFT_REG_WIDTH, 1.f, EXP_TIME); // 1.f);
        start_PMT_counter(pio, sm, dma_chan, capture_buf, buf_size_words, TRIG, true);
        dma_channel_wait_for_finish_blocking(dma_chan);
        // Disable PIO state machine
        printf("Run: %d\r\n", test);
        pio_sm_set_enabled(pio, sm, false);
        print_capture_buf(capture_buf, buf_size_words, CAPTURE_PIN_COUNT, buf_size_words);
        // Free memory after experiment
        //irq_clear(pio_get_dreq(pio, sm, false));
        free(capture_buf);
        test++;
    }
}