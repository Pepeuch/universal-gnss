// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
using gpio_num_t = int;
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) >= 0 && (pin) <= 48)
#define GPIO_IS_VALID_GPIO(pin) GPIO_IS_VALID_OUTPUT_GPIO(pin)
int gpio_reset_pin(gpio_num_t);
