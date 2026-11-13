#include "ug_rtcm.h"

#include <assert.h>
#include <string.h>

int main(void)
{
  uint32_t crc = 123;
  assert(!ug_rtcm_crc24q(NULL, 1, &crc) && crc == 123);
  assert(!ug_rtcm_crc24q(NULL, 0, NULL));
  assert(ug_rtcm_crc24q(NULL, 0, &crc) && crc == 0);
  assert(ug_rtcm_crc24q((const uint8_t*)"123456789", 9, &crc) && crc == 0xCDE703);
  assert(!ug_rtcm_frame_valid(NULL, 0));
  uint8_t frame[1030];
  for (size_t payload = 0; payload <= 1023; ++payload)
  {
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xD3;
    frame[1] = (uint8_t)(payload >> 8);
    frame[2] = (uint8_t)payload;
    for (size_t i = 0; i < payload; ++i)
    {
      frame[3 + i] = (uint8_t)i;
    }
    assert(ug_rtcm_crc24q(frame, payload + 3, &crc));
    frame[payload + 3] = (uint8_t)(crc >> 16);
    frame[payload + 4] = (uint8_t)(crc >> 8);
    frame[payload + 5] = (uint8_t)crc;
    assert(ug_rtcm_frame_valid(frame, payload + 6));
    assert(!ug_rtcm_frame_valid(frame, payload + 5));
    frame[payload + 5] ^= 1;
    assert(!ug_rtcm_frame_valid(frame, payload + 6));
    frame[payload + 5] ^= 1;
    frame[1] |= 0x80;
    /* Malformed reserved bits with a valid CRC must still be rejected. */
    assert(ug_rtcm_crc24q(frame, payload + 3, &crc));
    frame[payload + 3] = (uint8_t)(crc >> 16);
    frame[payload + 4] = (uint8_t)(crc >> 8);
    frame[payload + 5] = (uint8_t)crc;
    assert(!ug_rtcm_frame_valid(frame, payload + 6));
  }
  assert(!ug_rtcm_frame_valid(frame, sizeof(frame)));
  assert(!ug_rtcm_frame_valid(frame, SIZE_MAX));
  for (size_t size = 0; size < 6; ++size)
  {
    assert(!ug_rtcm_frame_valid(frame, size));
  }
  /* Bad preamble and inconsistent declared length, each with a correct CRC. */
  for (int bad_header = 0; bad_header < 2; ++bad_header)
  {
    memset(frame, 0, 6);
    frame[0] = bad_header == 0 ? 0x7E : 0xD3;
    frame[2] = bad_header == 1 ? 1 : 0;
    assert(ug_rtcm_crc24q(frame, 3, &crc));
    frame[3] = (uint8_t)(crc >> 16);
    frame[4] = (uint8_t)(crc >> 8);
    frame[5] = (uint8_t)crc;
    assert(!ug_rtcm_frame_valid(frame, 6));
  }
  return 0;
}
