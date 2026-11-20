// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_s3_radio_policy.h"
#include <assert.h>

int main(void)
{
  // Distinct legal initial values reveal writes even when they would happen
  // to equal the current SDK defaults.
  const wifi_init_config_t baseline = {.nvs_enable = true,
                                       .static_rx_buf_num = 12,
                                       .dynamic_rx_buf_num = 30,
                                       .dynamic_tx_buf_num = 28,
                                       .ampdu_rx_enable = 1,
                                       .ampdu_tx_enable = 1,
                                       .rx_ba_win = 6,
                                       .tx_buf_type = 1,
                                       .static_tx_buf_num = 9,
                                       .rx_mgmt_buf_num = 7,
                                       .espnow_max_encrypt_num = 7};
  const int expected[][6] = {
      {12, 30, 28, 1, 1, 6},
      {6, 30, 28, 1, 1, 6},
      {6, 16, 16, 1, 1, 6},
      {4, 16, 16, 0, 0, 0},
      {6, 16, 16, 0, 0, 0},
  };
  for (int radio_owner = 0; radio_owner <= 1; ++radio_owner)
  {
    wifi_init_config_t config = baseline;
    ug_apply_s3_radio_policy(&config, radio_owner != 0);
    const int* row = expected[radio_owner && CONFIG_IDF_TARGET_ESP32S3 ? UG_S3_RADIO_PROFILE : 0];
    assert(config.static_rx_buf_num == row[0] && config.dynamic_rx_buf_num == row[1] &&
           config.dynamic_tx_buf_num == row[2] && config.ampdu_rx_enable == row[3] &&
           config.ampdu_tx_enable == row[4] && config.rx_ba_win == row[5]);
    assert(config.nvs_enable == baseline.nvs_enable && config.tx_buf_type == baseline.tx_buf_type &&
           config.static_tx_buf_num == baseline.static_tx_buf_num &&
           config.rx_mgmt_buf_num == baseline.rx_mgmt_buf_num &&
           config.espnow_max_encrypt_num == baseline.espnow_max_encrypt_num);
  }
  return 0;
}
