// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define UG_PAIRING_RECORD_SIZE 32
#define UG_PAIRING_LINE_MAX 128
typedef struct
{
  bool paired;
  uint8_t channel, peer[6], key[16];
} ug_pairing_t;
typedef enum
{
  UG_PAIR_GET,
  UG_PAIR_SET,
  UG_PAIR_CLEAR,
  UG_PAIR_RUN
} ug_pair_command_kind_t;
typedef struct
{
  ug_pair_command_kind_t kind;
  ug_pairing_t pairing;
} ug_pair_command_t;
/* All parsers are bounded and leave the output untouched on failure. */
bool ug_pairing_from_text(const char* peer, const char* key, unsigned channel, ug_pairing_t* out);
bool ug_pairing_encode(const ug_pairing_t*, uint8_t out[UG_PAIRING_RECORD_SIZE]);
bool ug_pairing_decode(const uint8_t*, size_t size, ug_pairing_t*);
bool ug_pairing_sender(const uint8_t peer[6],
                       const uint8_t source[6],
                       const uint8_t destination[6],
                       const uint8_t local[6]);
bool ug_pairing_open(const char* line, size_t size, char nonce[17]);
bool ug_pairing_command(const char* line, size_t size, ug_pair_command_t*);
#ifdef __cplusplus
}
#endif
