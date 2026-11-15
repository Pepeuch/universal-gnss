// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
namespace ug::embedded
{
void InitializeMemoryDiagnostics();
void PrintMemoryDiagnostics();
bool MemoryTraceCommand(bool start, std::string& error);
void SampleWebMemory();
bool WebJsonMemoryHealthy();
bool RunMemoryJsonStress(std::string& error);
class WebMemoryScope
{
public:
  explicit WebMemoryScope(std::string_view uri);
  ~WebMemoryScope();
  WebMemoryScope(const WebMemoryScope&) = delete;
  WebMemoryScope& operator=(const WebMemoryScope&) = delete;

private:
  std::int64_t started_{};
  std::size_t route_{};
};
}  // namespace ug::embedded
