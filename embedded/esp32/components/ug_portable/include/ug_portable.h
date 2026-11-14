// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define UG_PORTABLE_JSON_MAX 512
#define UG_PORTABLE_RECORD_SIZE 24
#define UG_BOARD_S3 1
#define UG_BOARD_C3 2
#define UG_OUTPUT_GNSS 0
#define UG_OUTPUT_USB 1
#define UG_OUTPUT_UART 2
typedef struct
{
  uint8_t board, output, model, channel;
  int8_t tx, rx;
  uint32_t baud;
} ug_portable_t;
/* Flat JSON v1, exact known fields; no arbitrary string/secret/identity values.
 * Parsers leave output untouched on failure. No allocation, SDK or I/O. */
ug_portable_t ug_portable_default(uint8_t board);
bool ug_portable_valid(const ug_portable_t*);
bool ug_portable_pin(uint8_t board, int pin);
bool ug_portable_parse(const char*, size_t, ug_portable_t*);
bool ug_portable_json(const ug_portable_t*, char*, size_t);
bool ug_portable_record(const ug_portable_t*, uint8_t out[UG_PORTABLE_RECORD_SIZE]);
bool ug_portable_restore(const uint8_t*, size_t, ug_portable_t*);
const char* ug_portable_model(unsigned);
#ifdef __cplusplus
}
#endif
