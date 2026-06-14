#pragma once
#include <stdbool.h>
#include "driver/gpio.h"

typedef struct {
    float temperature;
    float humidity;
} dht11_data_t;

bool dht11_init(gpio_num_t pin);
bool dht11_read(dht11_data_t *out);
