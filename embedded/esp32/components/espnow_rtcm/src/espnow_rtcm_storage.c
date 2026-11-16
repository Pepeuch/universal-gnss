/* SPDX-License-Identifier: GPL-3.0-only */
#include "espnow_rtcm_storage.h"
#include <string.h>
bool espnow_rtcm_storage_reserve(espnow_rtcm_storage_t* slot, void* (*allocate)(size_t))
{
  if (!slot || !allocate || !slot->enter || !slot->exit)
    return false;
  slot->enter();
  const bool attempted = slot->attempted;
  slot->attempted = true;
  slot->exit();
  if (attempted)
    return false;
  // Never allocate or initialize a large payload while interrupts are masked.
  espnow_rtcm_storage_data_t* data = allocate(sizeof(*data));
  if (!data)
    return false;
  memset(data, 0, sizeof(*data));
  slot->enter();
  slot->data = data;
  slot->exit();
  return true;
}
espnow_rtcm_storage_data_t* espnow_rtcm_storage_get(const espnow_rtcm_storage_t* slot)
{
  slot->enter();
  espnow_rtcm_storage_data_t* data = slot->data;
  slot->exit();
  return data;
}
