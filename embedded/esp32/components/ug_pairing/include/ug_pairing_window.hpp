// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstdint>
namespace ug::embedded
{
class PairingWindow
{
public:
  bool Open(std::int64_t now_us)
  {
    if (now_us < 0 || now_us > INT64_MAX - 120000000)
      return false;
    start_ = now_us;
    end_ = now_us + 120000000;
    return true;
  }
  bool Active(std::int64_t now_us) const
  {
    return end_ && now_us >= start_ && now_us < end_;
  }
  void Close()
  {
    end_ = 0;
  }

private:
  std::int64_t start_{}, end_{};
};
}  // namespace ug::embedded
