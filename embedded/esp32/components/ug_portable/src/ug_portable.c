// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_portable.h"
#include "ug_rtcm.h"
#include <stdio.h>
#include <string.h>
static const char* const models[] = {"none", "auto", "NMEA", "UM980", "UM981", "UM982", "ZED-F9P"};
const char* ug_portable_model(unsigned n)
{
  return n < 7 ? models[n] : "invalid";
}
ug_portable_t ug_portable_default(uint8_t board)
{
  return (ug_portable_t){.board = board,
                         .output = board == UG_BOARD_S3 ? UG_OUTPUT_GNSS : UG_OUTPUT_USB,
                         .model = board == UG_BOARD_S3 ? 1 : 0,
                         .channel = 1,
                         .tx = -1,
                         .rx = -1,
                         .baud = 115200};
}
bool ug_portable_pin(uint8_t board, int pin)
{
  if (board == UG_BOARD_S3)
  {
    switch (pin)
    {
      case 1:
      case 2:
      case 15:
      case 16:
      case 17:
      case 18:
      case 38:
      case 39:
      case 40:
      case 41:
      case 42:
      case 43:
      case 44:
      case 47:
      case 48:
        return true;
      default:
        return false;
    }
  }
  return board == UG_BOARD_C3 && (pin == 4 || pin == 5 || pin == 6 || pin == 7 || pin == 10);
}
bool ug_portable_valid(const ug_portable_t* p)
{
  if (!p || p->channel < 1 || p->channel > 11)
    return false;
  if (p->board == UG_BOARD_S3)
    return p->output == UG_OUTPUT_GNSS && p->model >= 1 && p->model < 7 && p->baud >= 9600 &&
           p->baud <= 921600 &&
           ((p->tx == -1 && p->rx == -1) || (ug_portable_pin(p->board, p->tx) &&
                                             ug_portable_pin(p->board, p->rx) && p->tx != p->rx));
  if (p->board != UG_BOARD_C3 || p->model || p->rx != -1)
    return false;
  if (p->output == UG_OUTPUT_USB)
    return p->tx == -1 && p->baud == 115200;
  return p->output == UG_OUTPUT_UART && ug_portable_pin(p->board, p->tx) &&
         (p->baud == 115200 || p->baud == 230400 || p->baud == 460800 || p->baud == 921600);
}
/* Intentionally bounded subset of JSON: flat object, fixed ASCII enum strings,
 * decimal integers. Escapes, nested values, duplicate/missing/unknown keys fail. */
typedef struct
{
  const char* p;
  const char* end;
} reader_t;
static void space(reader_t* r)
{
  while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' || *r->p == '\r' || *r->p == '\n'))
    ++r->p;
}
static bool token(reader_t* r, char c)
{
  space(r);
  if (r->p == r->end || *r->p != c)
    return false;
  ++r->p;
  return true;
}
static bool string(reader_t* r, char* out, size_t capacity)
{
  if (!token(r, '"'))
    return false;
  size_t n = 0;
  while (r->p < r->end && *r->p != '"')
  {
    const char c = *r->p++;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
          c == '_') ||
        n + 1 >= capacity)
      return false;
    out[n++] = c;
  }
  if (r->p == r->end)
    return false;
  ++r->p;
  out[n] = 0;
  return true;
}
static bool integer(reader_t* r, int* out)
{
  space(r);
  bool minus = r->p < r->end && *r->p == '-';
  if (minus)
    ++r->p;
  if (r->p == r->end || *r->p < '0' || *r->p > '9')
    return false;
  const char* start = r->p;
  int value = 0;
  while (r->p < r->end && *r->p >= '0' && *r->p <= '9')
  {
    if (value > 1000000)
      return false;
    value = value * 10 + (*r->p++ - '0');
  }
  if (r->p - start > 1 && *start == '0')
    return false;
  if (minus && !value)
    return false;
  *out = minus ? -value : value;
  return true;
}
bool ug_portable_parse(const char* text, size_t size, ug_portable_t* out)
{
  if (!text || !out || !size || size > UG_PORTABLE_JSON_MAX)
    return false;
  static const char* const keys[] = {"format",
                                     "schema",
                                     "board",
                                     "profile",
                                     "ethernet",
                                     "output",
                                     "uart_tx",
                                     "uart_rx",
                                     "uart_baud",
                                     "gnss_model",
                                     "radio_channel"};
  reader_t r = {text, text + size};
  ug_portable_t p = {0};
  unsigned seen = 0;
  uint8_t role = 0, ethernet = 0;
  if (!token(&r, '{'))
    return false;
  for (unsigned item = 0; item < 11; ++item)
  {
    char key[32], value[32];
    int number = 0;
    unsigned index = 0;
    if ((item && !token(&r, ',')) || !string(&r, key, sizeof(key)) || !token(&r, ':'))
      return false;
    while (index < 11 && strcmp(key, keys[index]))
      ++index;
    if (index == 11 || (seen & (1u << index)))
      return false;
    seen |= 1u << index;
    if (index == 1 || index == 6 || index == 7 || index == 8 || index == 10)
    {
      if (!integer(&r, &number))
        return false;
      if (index == 1 && number != 1)
        return false;
      if (index == 6 || index == 7)
      {
        if (number < -1 || number > 48)
          return false;
        if (index == 6)
          p.tx = (int8_t)number;
        else
          p.rx = (int8_t)number;
      }
      if (index == 8)
      {
        if (number < 9600 || number > 921600)
          return false;
        p.baud = (uint32_t)number;
      }
      if (index == 10)
      {
        if (number < 1 || number > 11)
          return false;
        p.channel = (uint8_t)number;
      }
    }
    else
    {
      if (!string(&r, value, sizeof(value)))
        return false;
      switch (index)
      {
        case 0:
          if (strcmp(value, "ug-esp32-portable"))
            return false;
          break;
        case 2:
          if (!strcmp(value, "waveshare-s3-eth"))
            p.board = UG_BOARD_S3;
          else if (!strcmp(value, "supermini-c3"))
            p.board = UG_BOARD_C3;
          else
            return false;
          break;
        case 3:
          if (!strcmp(value, "gateway"))
            role = UG_BOARD_S3;
          else if (!strcmp(value, "rover"))
            role = UG_BOARD_C3;
          else
            return false;
          break;
        case 4:
          if (!strcmp(value, "w5500-onboard"))
            ethernet = UG_BOARD_S3;
          else if (!strcmp(value, "none"))
            ethernet = UG_BOARD_C3;
          else
            return false;
          break;
        case 5:
          if (!strcmp(value, "gnss"))
            p.output = UG_OUTPUT_GNSS;
          else if (!strcmp(value, "usb"))
            p.output = UG_OUTPUT_USB;
          else if (!strcmp(value, "uart"))
            p.output = UG_OUTPUT_UART;
          else
            return false;
          break;
        case 9: {
          unsigned m = 0;
          while (m < 7 && strcmp(value, models[m]))
            ++m;
          if (m == 7)
            return false;
          p.model = (uint8_t)m;
          break;
        }
      }
      /* Board-dependent string equality is checked after parsing independent of key order. */
    }
  }
  if (!token(&r, '}'))
    return false;
  space(&r);
  if (r.p != r.end || !ug_portable_valid(&p))
    return false;
  if (role != p.board || ethernet != p.board)
    return false;
  *out = p;
  return true;
}
bool ug_portable_json(const ug_portable_t* p, char* out, size_t size)
{
  if (!out || !ug_portable_valid(p))
    return false;
  int n = snprintf(out,
                   size,
                   "{\"format\":\"ug-esp32-portable\",\"schema\":1,\"board\":\"%s\",\"profile\":\"%"
                   "s\",\"ethernet\":\"%s\",\"output\":\"%s\",\"uart_tx\":%d,\"uart_rx\":%d,\"uart_"
                   "baud\":%lu,\"gnss_model\":\"%s\",\"radio_channel\":%u}",
                   p->board == UG_BOARD_S3 ? "waveshare-s3-eth" : "supermini-c3",
                   p->board == UG_BOARD_S3 ? "gateway" : "rover",
                   p->board == UG_BOARD_S3 ? "w5500-onboard" : "none",
                   p->output == UG_OUTPUT_GNSS  ? "gnss"
                   : p->output == UG_OUTPUT_USB ? "usb"
                                                : "uart",
                   p->tx,
                   p->rx,
                   (unsigned long)p->baud,
                   models[p->model],
                   p->channel);
  return n > 0 && (size_t)n < size;
}
bool ug_portable_record(const ug_portable_t* p, uint8_t out[UG_PORTABLE_RECORD_SIZE])
{
  if (!out || !ug_portable_valid(p))
    return false;
  uint8_t data[UG_PORTABLE_RECORD_SIZE] = {'U',
                                           'G',
                                           'P',
                                           'C',
                                           1,
                                           p->board,
                                           p->output,
                                           p->model,
                                           p->channel,
                                           (uint8_t)p->tx,
                                           (uint8_t)p->rx};
  for (unsigned i = 0; i < 4; ++i)
    data[12 + i] = (uint8_t)(p->baud >> (8 * i));
  uint32_t crc;
  if (!ug_rtcm_crc24q(data, 21, &crc))
    return false;
  data[21] = (uint8_t)(crc >> 16);
  data[22] = (uint8_t)(crc >> 8);
  data[23] = (uint8_t)crc;
  memcpy(out, data, sizeof(data));
  return true;
}
bool ug_portable_restore(const uint8_t* data, size_t size, ug_portable_t* out)
{
  uint32_t crc;
  if (!data || !out || size != UG_PORTABLE_RECORD_SIZE || memcmp(data, "UGPC\1", 5) || data[11] ||
      data[16] || data[17] || data[18] || data[19] || data[20] || !ug_rtcm_crc24q(data, 21, &crc) ||
      crc != ((uint32_t)data[21] << 16 | ((uint32_t)data[22] << 8) | data[23]))
    return false;
  ug_portable_t p = {.board = data[5],
                     .output = data[6],
                     .model = data[7],
                     .channel = data[8],
                     .tx = (int8_t)data[9],
                     .rx = (int8_t)data[10]};
  for (unsigned i = 0; i < 4; ++i)
    p.baud |= (uint32_t)data[12 + i] << (8 * i);
  if (!ug_portable_valid(&p))
    return false;
  *out = p;
  return true;
}
