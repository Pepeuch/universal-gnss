#pragma once
#include "esp_err.h"
#include <stdint.h>
#define ESP_MAC_WIFI_STA 0
esp_err_t esp_read_mac(uint8_t*, int);
