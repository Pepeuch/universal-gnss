#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Borrows data for this call only; no allocation, clock or retained state.
 * NULL data is allowed only for size 0. Invalid arguments return false.
 * CRC output is unchanged on error. */
bool ug_rtcm_crc24q(const uint8_t* data, size_t size, uint32_t* crc);

/* Complete RTCM3 container, reserved bits zero, 6..1029 bytes, valid CRC24Q.
 * Does not validate message-specific payload semantics. */
bool ug_rtcm_frame_valid(const uint8_t* data, size_t size);

#ifdef __cplusplus
}
#endif
