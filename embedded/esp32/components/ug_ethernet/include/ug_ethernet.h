// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
  int mosi, miso, clock, chip_select, interrupt, reset;
} ug_ethernet_pins_t;
// Network initialization is owned by the application; no Wi-Fi association here.
esp_err_t ug_ethernet_start_w5500(const ug_ethernet_pins_t* pins);
bool ug_ethernet_ready(void);
#ifdef __cplusplus
}
#endif
