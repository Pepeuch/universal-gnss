// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_pairing.h"
#include "ug_rtcm.h"
#include <stdio.h>
#include <string.h>
static int hex(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
static bool unicast(const uint8_t* p)
{
  unsigned any = 0;
  for (size_t i = 0; i < 6; ++i)
    any |= p[i];
  return any && !(p[0] & 1);
}
static bool key_valid(const uint8_t* key)
{
  unsigned any = 0;
  for (size_t i = 0; i < 16; ++i)
    any |= key[i];
  return any != 0;
}
bool ug_pairing_from_text(const char* peer, const char* key, unsigned channel, ug_pairing_t* out)
{
  if (!peer || !key || !out || strlen(peer) != 17 || strlen(key) != 32 || channel < 1 ||
      channel > 11)
    return false;
  ug_pairing_t p = {.paired = true, .channel = (uint8_t)channel};
  for (size_t i = 0; i < 6; ++i)
  {
    const int a = hex(peer[i * 3]), b = hex(peer[i * 3 + 1]);
    if (a < 0 || b < 0 || (i < 5 && peer[i * 3 + 2] != ':'))
      return false;
    p.peer[i] = (uint8_t)((a << 4) | b);
  }
  for (size_t i = 0; i < 16; ++i)
  {
    const int a = hex(key[i * 2]), b = hex(key[i * 2 + 1]);
    if (a < 0 || b < 0)
      return false;
    p.key[i] = (uint8_t)((a << 4) | b);
  }
  if (!unicast(p.peer) || !key_valid(p.key))
    return false;
  *out = p;
  return true;
}
bool ug_pairing_encode(const ug_pairing_t* p, uint8_t out[UG_PAIRING_RECORD_SIZE])
{
  if (!p || !out ||
      (p->paired && (p->channel < 1 || p->channel > 11 || !unicast(p->peer) || !key_valid(p->key))))
    return false;
  uint8_t bytes[UG_PAIRING_RECORD_SIZE] = {'U', 'G', 'P', '1', 1, 0, 1};
  if (p->paired)
  {
    bytes[5] = 1;
    bytes[6] = p->channel;
    memcpy(bytes + 7, p->peer, 6);
    memcpy(bytes + 13, p->key, 16);
  }
  uint32_t crc;
  if (!ug_rtcm_crc24q(bytes, 29, &crc))
    return false;
  bytes[29] = crc >> 16;
  bytes[30] = crc >> 8;
  bytes[31] = crc;
  memcpy(out, bytes, sizeof(bytes));
  return true;
}
bool ug_pairing_decode(const uint8_t* bytes, size_t size, ug_pairing_t* out)
{
  if (!bytes || !out || size != UG_PAIRING_RECORD_SIZE || memcmp(bytes, "UGP1", 4) ||
      bytes[4] != 1 || bytes[5] > 1)
    return false;
  uint32_t crc;
  if (!ug_rtcm_crc24q(bytes, 29, &crc) ||
      crc != ((uint32_t)bytes[29] << 16 | (uint32_t)bytes[30] << 8 | bytes[31]))
    return false;
  ug_pairing_t p = {.paired = bytes[5] != 0, .channel = bytes[6]};
  memcpy(p.peer, bytes + 7, 6);
  memcpy(p.key, bytes + 13, 16);
  uint8_t canonical[UG_PAIRING_RECORD_SIZE];
  if (!ug_pairing_encode(&p, canonical) || memcmp(bytes, canonical, size))
    return false;
  *out = p;
  return true;
}
bool ug_pairing_command(const char* line, size_t size, ug_pair_command_t* out)
{
  if (!line || !out || size > UG_PAIRING_LINE_MAX || memchr(line, 0, size))
    return false;
  ug_pair_command_t c = {0};
  if (size == strlen("UGCFG/1 GET") && !memcmp(line, "UGCFG/1 GET", size))
    c.kind = UG_PAIR_GET;
  else if (size == strlen("UGCFG/1 CLEAR CONFIRM_UNPAIR") &&
           !memcmp(line, "UGCFG/1 CLEAR CONFIRM_UNPAIR", size))
    c.kind = UG_PAIR_CLEAR;
  else if (size == strlen("UGCFG/1 RUN CONFIRM_RTCM") &&
           !memcmp(line, "UGCFG/1 RUN CONFIRM_RTCM", size))
    c.kind = UG_PAIR_RUN;
  else
  {
    char text[UG_PAIRING_LINE_MAX + 1];
    memcpy(text, line, size);
    text[size] = 0;
    char peer[18], key[33], channel[3];
    int end = 0;
    if (sscanf(text, "UGCFG/1 SET %17s %2s %32s CONFIRM_PAIR%n", peer, channel, key, &end) != 3 ||
        (size_t)end != size)
      return false;
    unsigned value = 0;
    for (unsigned i = 1; i <= 11; ++i)
    {
      char n[3];
      snprintf(n, sizeof(n), "%u", i);
      if (!strcmp(n, channel))
        value = i;
    }
    if (!ug_pairing_from_text(peer, key, value, &c.pairing))
      return false;
    char canonical[UG_PAIRING_LINE_MAX + 1];
    const int n = snprintf(
        canonical, sizeof(canonical), "UGCFG/1 SET %s %s %s CONFIRM_PAIR", peer, channel, key);
    if (n < 0 || (size_t)n != size || memcmp(line, canonical, size))
      return false;
    c.kind = UG_PAIR_SET;
  }
  *out = c;
  return true;
}

bool ug_pairing_open(const char* line, size_t size, char nonce[17])
{
  if (!line || !nonce || size != 29 || memcmp(line, "UGCFG/1 OPEN ", 13))
    return false;
  for (size_t i = 13; i < 29; ++i)
    if (hex(line[i]) < 0)
      return false;
  memcpy(nonce, line + 13, 16);
  nonce[16] = 0;
  return true;
}

bool ug_pairing_sender(const uint8_t peer[6],
                       const uint8_t source[6],
                       const uint8_t destination[6],
                       const uint8_t local[6])
{
  return peer && source && destination && local && unicast(peer) && unicast(local) &&
         !memcmp(peer, source, 6) && !memcmp(local, destination, 6);
}
