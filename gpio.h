#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#define GPIO_PINS (CHAR_BIT * sizeof(gpio_reg_t))

struct GPIO;
typedef unsigned int gpio_reg_t;

struct GPIO *gpio_open();
void gpio_close(struct GPIO *gpio);
gpio_reg_t gpio_get_inputs(struct GPIO *gpio);
gpio_reg_t gpio_get_and_print_delta(struct GPIO *gpio, gpio_reg_t prev_gpio_reg);
bool gpio_get_pin(struct GPIO *gpio, size_t pin);
