// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_settings.hpp"
#include <vector>
namespace ug::embedded
{
enum class BlobRead
{
  kFound,
  kMissing,
  kError
};
class SettingsStorage
{
public:
  virtual ~SettingsStorage() = default;
  // Implementations must bound reads and atomically replace a single record.
  virtual BlobRead Read(unsigned slot, std::vector<std::uint8_t>& bytes) = 0;
  virtual bool Write(unsigned slot, const std::vector<std::uint8_t>& bytes) = 0;
};
class SettingsStore
{
public:
  static constexpr std::size_t kRecordLimit = 8192 + 23;
  SettingsStore(SettingsStorage& storage, Capabilities capabilities);
  // A malformed/incompatible record is preserved. One intact bank permits recovery.
  bool Load(Settings& out, std::string& error);
  bool Save(const Settings&, std::string& error);
  bool recovered() const
  {
    return recovered_;
  }
  std::uint64_t generation() const
  {
    return generation_;
  }

private:
  SettingsStorage& storage_;
  Capabilities capabilities_;
  std::uint64_t generation_{0};
  unsigned active_{1};
  bool loaded_{false}, recovered_{false};
};
}  // namespace ug::embedded
