/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "espnow_rtcm_codec.h"
/* Payload lifetime is the whole boot, independently of SDK/callback lifetime.
 * Synchronization, activation claim, TX epoch/quarantine and stats stay outside.
 * Ring cursors retain the existing mailbox layout; this storage is INTERNAL.
 */
typedef struct
{
  espnow_rtcm_codec_t stream, radio;
  espnow_rtcm_mailbox_t queue;
  espnow_rtcm_message_t message;
} espnow_rtcm_storage_data_t;
typedef struct
{
  espnow_rtcm_storage_data_t* data;
  bool attempted;
  void (*enter)(void);
  void (*exit)(void);
} espnow_rtcm_storage_t;
/* One allocation attempt per boot. Failure never falls back or starts the SDK.
 * A concurrent reserver fails closed; only the startup owner initializes data.
 */
bool espnow_rtcm_storage_reserve(espnow_rtcm_storage_t*, void* (*allocate)(size_t));
espnow_rtcm_storage_data_t* espnow_rtcm_storage_get(const espnow_rtcm_storage_t*);
