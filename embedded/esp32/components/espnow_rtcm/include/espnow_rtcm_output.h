/* GPL-3.0-only; adapted from the bounded historical ESP-NOW USB output loop.
 * See embedded/esp32/licenses/legacy-GPL-3.0.txt and migration/legacy_inventory.json. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Synchronous borrowed bytes; writer must return within wait_ms, never retain data. */
typedef int (*espnow_rtcm_write_fn)(const uint8_t*, size_t, uint32_t wait_ms, void*);
typedef struct
{
  espnow_rtcm_write_fn write;
  uint32_t (*clock_ms)(void*);
  void (*idle)(void*);
  void* context;
} espnow_rtcm_output_ops_t;
typedef struct
{
  size_t accepted;
  bool complete;
} espnow_rtcm_output_result_t;
/* Caller already validated RTCM. Bounded 200-ms enqueue budget, not physical delivery. */
espnow_rtcm_output_result_t
espnow_rtcm_output_frame(const espnow_rtcm_output_ops_t*, const uint8_t*, size_t);
#ifdef __cplusplus
}
#endif
