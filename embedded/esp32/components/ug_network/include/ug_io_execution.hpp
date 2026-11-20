// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
namespace ug::embedded
{
// Native adapter execution policy, not a change to the shared transport contract.
// A connection has exactly one owner at a time. The helper may wait; the I/O
// owner must never wait for a socket. Nesting restores the caller's policy.
enum class IoExecution
{
  kDefault,
  kNonblocking,
  kBlocking
};
inline thread_local IoExecution io_execution = IoExecution::kDefault;
class IoExecutionScope
{
public:
  explicit IoExecutionScope(IoExecution policy) : previous_(io_execution)
  {
    io_execution = policy;
  }
  ~IoExecutionScope()
  {
    io_execution = previous_;
  }
  IoExecutionScope(const IoExecutionScope&) = delete;
  IoExecutionScope& operator=(const IoExecutionScope&) = delete;

private:
  IoExecution previous_;
};
inline bool NonblockingIo(bool configured)
{
  return io_execution == IoExecution::kNonblocking ||
         (io_execution == IoExecution::kDefault && configured);
}
}  // namespace ug::embedded
