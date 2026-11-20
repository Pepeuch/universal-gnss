// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// One-peer association: domain-separated PMK from the existing random LMK.
// No fixed shared secret and no new persistent record. Output is cleared on
// backend failure. Caller must erase its temporary PMK after SDK configuration.
bool ug_pairing_derive_pmk(const uint8_t lmk[16], uint8_t pmk[16]);
#ifdef __cplusplus
}
#endif
