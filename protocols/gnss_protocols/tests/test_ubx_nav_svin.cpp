// SPDX-License-Identifier: LGPL-3.0-only
#include "universal_gnss_protocols/ubx_parser.hpp"
#include <cstdlib>
#include <limits>
using namespace universal_gnss_protocols;
static void Check(bool condition)
{
  if (!condition)
    std::abort();
}
static void Put(ByteVector& p, std::size_t offset, std::uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i)
    p[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
int main()
{
  UbxFrame f;
  f.class_id = 1;
  f.message_id = 0x3b;
  f.checksum_status = ChecksumStatus::kValid;
  f.timestamp_ns = 123;
  f.payload.resize(40);
  auto& p = f.payload;
  Put(p, 4, 604799999);
  Put(p, 8, 3600);
  Put(p, 12, static_cast<std::uint32_t>(-123456789));
  p[24] = static_cast<std::uint8_t>(-99);
  Put(p, 16, INT32_MAX);
  p[25] = 99;
  Put(p, 20, static_cast<std::uint32_t>(INT32_MIN));
  p[26] = static_cast<std::uint8_t>(-1);
  Put(p, 28, 12345);
  Put(p, 32, 9999);
  p[36] = 1;
  const auto parsed = ParseUbxNavSvin(f);
  Check(parsed.record.has_value());
  const auto& record = *parsed.record;
  Check(record.timestamp_ns == 123 && record.i_tow_ms == 604799999 && record.duration_s == 3600);
  Check(record.mean_x_01mm == -12345678999LL && record.mean_y_01mm == 214748364799LL &&
        record.mean_z_01mm == -214748364801LL);
  Check(record.mean_accuracy_01mm == 12345 && record.observations == 9999 && record.valid &&
        !record.active);
  // Reserved bytes are ignored, not repurposed as undocumented validity bits.
  p[1] = 0xff;
  p[27] = 0xff;
  p[39] = 0xff;
  Check(ParseUbxNavSvin(f).record.has_value());
  p[37] = 1;
  Check(ParseUbxNavSvin(f).record->active);  // Preserve both wire flags.
  for (const auto offset : {0, 24, 25, 26, 36, 37})
  {
    auto bad = f;
    bad.payload[offset] = offset == 0 ? 1 : offset >= 36 ? 2 : 100;
    Check(ParseUbxNavSvin(bad).status == ParserStatus::kInvalidData);
  }
  auto bad = f;
  Put(bad.payload, 4, 604800000);
  Check(ParseUbxNavSvin(bad).status == ParserStatus::kInvalidData);
  for (std::size_t size = 0; size < 45; ++size)
  {
    if (size == 40)
      continue;
    bad = f;
    bad.payload.resize(size);
    Check(ParseUbxNavSvin(bad).status == ParserStatus::kInvalidData);
  }
  bad = f;
  bad.checksum_status = ChecksumStatus::kInvalid;
  Check(ParseUbxNavSvin(bad).status == ParserStatus::kInvalidData);
  bad = f;
  bad.message_id = 7;
  Check(ParseUbxNavSvin(bad).status == ParserStatus::kSkipped);
  bad = f;
  bad.class_id = 0x29;
  Check(ParseUbxNavSvin(bad).status == ParserStatus::kSkipped);
}
