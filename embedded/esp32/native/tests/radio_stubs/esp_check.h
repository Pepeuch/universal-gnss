// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
#define ESP_RETURN_ON_ERROR(expr, tag, ...)                                                        \
  do                                                                                               \
  {                                                                                                \
    (void)(tag);                                                                                   \
    const esp_err_t status_ = (expr);                                                              \
    if (status_ != ESP_OK)                                                                         \
      return status_;                                                                              \
  } while (0)
