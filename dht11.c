#include "dht11.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static gpio_num_t s_pin;

bool dht11_init(gpio_num_t pin)
{
    s_pin = pin;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 1);
    return true;
}

static bool wait_level(int level, uint32_t timeout_us)
{
    uint32_t elapsed = 0;
    while (gpio_get_level(s_pin) != level) {
        if (elapsed >= timeout_us) return false;
        esp_rom_delay_us(1);
        elapsed++;
    }
    return true;
}

bool dht11_read(dht11_data_t *out)
{
    uint8_t data[5] = {0};

    /* Start signal */
    gpio_set_direction(s_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(s_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(s_pin, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(s_pin, GPIO_MODE_INPUT);

    /* Wait for sensor response: low ~80us, then high ~80us */
    if (!wait_level(0, 100)) return false;
    if (!wait_level(1, 100)) return false;
    if (!wait_level(0, 100)) return false;

    /* Read 40 bits */
    for (int i = 0; i < 40; i++) {
        /* Each bit starts with ~50us low */
        if (!wait_level(1, 100)) return false;

        /* High duration: ~26-28us = '0', ~70us = '1' */
        esp_rom_delay_us(40);
        int bit = gpio_get_level(s_pin);
        data[i / 8] = (data[i / 8] << 1) | bit;

        if (!wait_level(0, 100)) return false;
    }

    /* Checksum */
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        return false;
    }

    out->humidity    = data[0] + data[1] * 0.1f;
    out->temperature = data[2] + data[3] * 0.1f;
    return true;
}
