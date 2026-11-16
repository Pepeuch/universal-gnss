// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
#include <cstddef>
using nvs_handle_t = unsigned;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
esp_err_t nvs_open(const char*, int, nvs_handle_t*);
esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, std::size_t*);
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, std::size_t);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char*);
void nvs_close(nvs_handle_t);
