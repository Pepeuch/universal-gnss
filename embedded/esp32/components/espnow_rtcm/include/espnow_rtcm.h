/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t espnow_rtcm_start(void);
/* BASE only: one NTRIP task owns this streaming parser. Never waits for radio. */
void espnow_rtcm_ntrip_reset(void);
void espnow_rtcm_ntrip_response(bool icy);
void espnow_rtcm_ntrip_bytes(const uint8_t* data, size_t len);

/* Gateway-owner only; queues a whole validated frame, never waits for radio. */
esp_err_t espnow_rtcm_send_frame(const uint8_t* frame, size_t len);
