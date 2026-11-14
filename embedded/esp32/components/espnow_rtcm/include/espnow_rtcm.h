/* Derived from esp32-rtk-gateway commit 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85.
 * GPL-3.0; see embedded/esp32/licenses/legacy-GPL-3.0.txt.
 * UG adaptations retain the original protocol; provenance: migration/legacy_inventory.json.
 */
#pragma once
#include "esp_err.h"
#include "espnow_rtcm_output.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Before start only; NULL/empty peer retains the explicit compile-time test peer. */
esp_err_t espnow_rtcm_configure(const char* peer_mac, uint8_t channel);
esp_err_t espnow_rtcm_start(void);
/* ROVER only, before start. Overrides USB; caller owns a pre-initialized bounded writer. */
esp_err_t espnow_rtcm_set_rover_writer(espnow_rtcm_write_fn, void* context);
/* BASE only: one NTRIP task owns this streaming parser. Never waits for radio. */
void espnow_rtcm_ntrip_reset(void);
void espnow_rtcm_ntrip_response(bool icy);
void espnow_rtcm_ntrip_bytes(const uint8_t* data, size_t len);

/* Gateway-owner only; queues a whole validated frame, never waits for radio. */
esp_err_t espnow_rtcm_send_frame(const uint8_t* frame, size_t len);
/* IDF monotonic receipt in wrapping milliseconds; queueing does not renew freshness. */
esp_err_t espnow_rtcm_send_frame_at(const uint8_t* frame, size_t len, uint32_t receipt_ms);

/* Invalidate pending transmissions; does not clear late-callback quarantine. */
void espnow_rtcm_invalidate_pending(void);

/* Snapshot counters; never emits diagnostics into the rover's RTCM USB stream. */
typedef struct
{
  bool started, tx_quarantined;
  uint8_t effective_channel;
  uint32_t queued;
  uint64_t dropped, invalid, tx_frames, tx_packets_acknowledged, tx_failures;
  uint64_t rx_packets, rx_frames, rx_crc16_errors, rx_crc24q_errors, reassembly_expired,
      reassembly_abandoned;
  uint64_t usb_bytes, usb_frames, usb_errors;
  uint64_t output_bytes, output_frames, output_errors;
} espnow_rtcm_stats_t;
void espnow_rtcm_get_stats(espnow_rtcm_stats_t* result);
