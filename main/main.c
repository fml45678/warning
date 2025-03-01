#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

// Function prototypes
void set_pixel(uint32_t pos, uint8_t r, uint8_t g, uint8_t b);
void update_leds(void);
void ws2812_init(void);

#define WS2812_PIN     4      // GPIO pin connected to WS2812B
#define LDR_PIN        ADC_CHANNEL_4  // GPIO 32 for light sensor
#define LED_COUNT      14
#define BLINK_DURATION 10000   // 10 seconds in milliseconds

static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_t *led_encoder = NULL;  // Moved this declaration up
static adc_oneshot_unit_handle_t adc1_handle;

// WS2812B timing (in RMT ticks at 10MHz)
#define T0H    4  // 0.4us
#define T0L    8  // 0.8us
#define T1H    8  // 0.8us
#define T1L    4  // 0.4us

static uint8_t led_data[LED_COUNT * 3];  // Changed to byte array (RGB)

// Remove this line since it's already declared at the top
// static rmt_encoder_t *led_encoder = NULL;

void ws2812_init(void) {
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = WS2812_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10MHz
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = T0H,
            .level1 = 0,
            .duration1 = T0L,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = T1H,
            .level1 = 0,
            .duration1 = T1L,
        },
        .flags.msb_first = 1
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder));
    ESP_ERROR_CHECK(rmt_enable(led_chan));
}

void app_main(void) {
    // Initialize WS2812B
    ws2812_init();
    
    // Initialize ADC for light sensor
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, LDR_PIN, &config));
    
    // Initialize all LEDs to off
    for (int i = 0; i < LED_COUNT; i++) {
        set_pixel(i, 0, 0, 0);
    }
    update_leds();
    
    int last_light_value = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, LDR_PIN, &last_light_value));
    int blink_start = 0;
    bool is_blinking = false;
    
    printf("Initial light value: %d\n", last_light_value);
    
    while (1) {
        int current_light;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, LDR_PIN, &current_light));
        
        printf("Current light: %d, Last light: %d, Diff: %d\n", 
               current_light, last_light_value, 
               abs(current_light - last_light_value));
        
        // Check for significant light change
        if (abs(current_light - last_light_value) > 200) {
            is_blinking = true;
            blink_start = xTaskGetTickCount() * portTICK_PERIOD_MS;
            printf("Light change detected! Starting blink\n");
        }
        
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        if (is_blinking) {
            if (current_time - blink_start < BLINK_DURATION) {
                if ((current_time / 500) % 2) {
                    for (int i = 0; i < LED_COUNT; i++) {
                        set_pixel(i, 255, 0, 0);  // Red color
                    }
                } else {
                    for (int i = 0; i < LED_COUNT; i++) {
                        set_pixel(i, 0, 0, 0);    // Off
                    }
                }
            } else {
                is_blinking = false;
                for (int i = 0; i < LED_COUNT; i++) {
                    set_pixel(i, 0, 0, 0);
                }
            }
        } else {
            for (int i = 0; i < LED_COUNT; i++) {
                set_pixel(i, 0, 0, 0);
            }
        }
        
        update_leds();
        
        last_light_value = current_light;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void set_pixel(uint32_t pos, uint8_t r, uint8_t g, uint8_t b) {
    if (pos < LED_COUNT) {
        led_data[pos * 3 + 0] = g;  // Green first
        led_data[pos * 3 + 1] = r;  // Red second
        led_data[pos * 3 + 2] = b;  // Blue third
    }
}

void update_leds(void) {
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags.eot_level = 0
    };
    ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_data, sizeof(led_data), &tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
}