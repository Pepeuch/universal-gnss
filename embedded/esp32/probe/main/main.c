#include "ug_rtcm.h"

#include <assert.h>

void app_main(void)
{
  uint8_t frame[6] = {0xD3, 0, 0, 0, 0, 0};
  uint32_t crc = 0;
  assert(ug_rtcm_crc24q(frame, 3, &crc));
  frame[3] = (uint8_t)(crc >> 16);
  frame[4] = (uint8_t)(crc >> 8);
  frame[5] = (uint8_t)crc;
  assert(ug_rtcm_frame_valid(frame, sizeof(frame)));
}
