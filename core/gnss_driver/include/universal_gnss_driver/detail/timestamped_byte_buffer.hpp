// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace universal_gnss_driver::detail
{
// Session-owned, single-threaded byte storage. Adjacent equal timestamps may
// share a run, but an absent timestamp is distinct from every int64_t value.
// Cursors/pointers borrow this object and expire on its next mutation.
class TimestampedByteBuffer
{
  static constexpr std::size_t kPresent = std::size_t{1}
                                          << (std::numeric_limits<std::size_t>::digits - 1);
  static constexpr std::size_t kEndMask = kPresent - 1;
  struct Run
  {
    std::int64_t timestamp{};
    std::size_t end_and_present{};
    std::size_t end() const
    {
      return end_and_present & kEndMask;
    }
    std::optional<std::int64_t> stamp() const
    {
      return end_and_present & kPresent ? std::optional<std::int64_t>(timestamp) : std::nullopt;
    }
  };

public:
  TimestampedByteBuffer() = default;
  TimestampedByteBuffer(const TimestampedByteBuffer&) = default;
  TimestampedByteBuffer& operator=(const TimestampedByteBuffer&) = default;
  TimestampedByteBuffer(TimestampedByteBuffer&& other) noexcept
      : bytes_(std::move(other.bytes_)), runs_(std::move(other.runs_)),
        head_(std::exchange(other.head_, 0))
  {
  }
  TimestampedByteBuffer& operator=(TimestampedByteBuffer&& other) noexcept
  {
    if (this != &other)
    {
      bytes_ = std::move(other.bytes_);
      runs_ = std::move(other.runs_);
      head_ = std::exchange(other.head_, 0);
    }
    return *this;
  }
  class Cursor
  {
  public:
    bool valid() const
    {
      return current_ != end_;
    }
    std::uint8_t value() const
    {
      return *current_;
    }
    const std::optional<std::int64_t>& timestamp() const
    {
      return timestamp_;
    }
    void next()
    {
      if (++current_ == run_end_ && current_ != end_)
      {
        ++run_;
        timestamp_ = run_->stamp();
        run_end_ = base_ + run_->end();
      }
    }

  private:
    friend class TimestampedByteBuffer;
    Cursor(const TimestampedByteBuffer& owner, std::size_t index)
    {
      if (owner.empty())
        return;
      base_ = owner.bytes_.data();
      current_ = base_ + owner.head_ + index;
      end_ = base_ + owner.bytes_.size();
      if (current_ == end_)
        return;
      const auto it =
          std::upper_bound(owner.runs_.begin(),
                           owner.runs_.end(),
                           owner.head_ + index,
                           [](std::size_t offset, const Run& r) { return offset < r.end(); });
      run_ = &*it;
      run_end_ = base_ + run_->end();
      timestamp_ = run_->stamp();
    }
    const std::uint8_t* base_{};
    const std::uint8_t* current_{};
    const std::uint8_t* end_{};
    const std::uint8_t* run_end_{};
    const Run* run_{};
    std::optional<std::int64_t> timestamp_{};
  };

  std::size_t size() const
  {
    return bytes_.size() - head_;
  }
  bool empty() const
  {
    return size() == 0;
  }
  const std::uint8_t* data() const
  {
    return bytes_.empty() ? nullptr : bytes_.data() + head_;
  }
  std::uint8_t operator[](std::size_t index) const
  {
    return bytes_[head_ + index];
  }
  Cursor cursor(std::size_t index = 0) const
  {
    return Cursor(*this, index);
  }
  std::size_t byte_capacity() const
  {
    return bytes_.capacity();
  }
  std::size_t metadata_capacity() const
  {
    return runs_.capacity();
  }
  std::size_t retained_bytes() const
  {
    return byte_capacity() + metadata_capacity() * sizeof(Run);
  }

  // Source is caller-owned and must not alias the buffer. Growth is checked,
  // geometric and bounded by vector/position limits, never by timestamp value.
  void Append(const std::uint8_t* data, std::size_t count, std::optional<std::int64_t> timestamp)
  {
    if (!count)
      return;
    if (!data)
      Fail<std::invalid_argument>("null byte source");
    if (count > kEndMask - size())
      Fail<std::length_error>("timestamped byte buffer overflow");
    if (head_ && (count > bytes_.capacity() - bytes_.size() || count > kEndMask - bytes_.size()))
      Compact();
    const auto end = bytes_.size() + count;
    const bool next = runs_.empty() || runs_.back().stamp() != timestamp;
    if (next)
      Grow(runs_, runs_.size() + 1, true);
    Grow(bytes_, end);
    bytes_.insert(bytes_.end(), data, data + count);
    const Run run{timestamp.value_or(0), end | (timestamp ? kPresent : 0)};
    if (next)
      runs_.push_back(run);
    else
      runs_.back() = run;
  }
  void ErasePrefix(std::size_t count)
  {
    if (count > size())
      Fail<std::out_of_range>("timestamped prefix");
    if (count == size())
    {
      clear();
      return;
    }
    head_ += count;
    const auto keep =
        std::upper_bound(runs_.begin(), runs_.end(), head_, [](std::size_t offset, const Run& r) {
          return offset < r.end();
        });
    runs_.erase(runs_.begin(), keep);
  }
  // Retain at most twice the configured parser/detection bound once a call has
  // finished. Incoming FeedBytes is still inspected as one batch, preserving
  // selection/resynchronization semantics. Shrinking is best effort under OOM:
  // existing bytes/timestamps always survive, and a later call can retry.
  void ConstrainCapacity(std::size_t retained_limit) noexcept
  {
    const auto limit = std::max(retained_limit, size());
    const auto bound = limit > kEndMask / 2 ? kEndMask : limit * 2;
    const bool bytes_excess = bytes_.capacity() > bound;
    const bool runs_excess = runs_.capacity() > bound;
    if (!bytes_excess && !runs_excess)
      return;
    Compact();
    if (empty())
    {
      if (bytes_excess)
        std::vector<std::uint8_t>().swap(bytes_);
      if (runs_excess)
        std::vector<Run>().swap(runs_);
      return;
    }
#if defined(__cpp_exceptions)
    try
    {
      if (bytes_excess)
        bytes_.shrink_to_fit();
      if (runs_excess)
        runs_.shrink_to_fit();
    }
    catch (const std::bad_alloc&)
    { /* Preserve valid storage; a later call may retry. */
    }
#else
    // IDF deliberately disables C++ exceptions. Do not introduce an optional
    // allocation here that could terminate an otherwise healthy receiver.
    // Retain live storage; empty oversized buffers are still released above.
#endif
  }

  void clear()
  {
    bytes_.clear();
    runs_.clear();
    head_ = 0;
  }

private:
  template <class Error>
  [[noreturn]] static void Fail(const char* reason)
  {
#if defined(__cpp_exceptions)
    throw Error(reason);
#else
    (void)reason;
    std::abort();  // Same fatal allocation/range contract as std::vector without exceptions.
#endif
  }

  template <class T>
  static void Grow(std::vector<T>& v, std::size_t need, bool metadata = false)
  {
    if (need > v.max_size())
      Fail<std::length_error>("timestamped storage overflow");
    if (need > v.capacity())
    {
      // Metadata grows by 25%, bytes by 100%. Even one timestamp per byte
      // has bounded retained storage (<=22N+constant), rather than 24N with
      // exact legacy reserve. Sparse timestamps retain only a handful of runs.
      const auto increment =
          metadata ? v.capacity() / 4 + 1 : std::max<std::size_t>(1, v.capacity());
      const auto grown =
          v.capacity() > v.max_size() - increment ? v.max_size() : v.capacity() + increment;
      v.reserve(std::max(need, grown));
    }
  }
  void Compact()
  {
    const auto active = size();
    std::move(bytes_.begin() + static_cast<std::ptrdiff_t>(head_), bytes_.end(), bytes_.begin());
    bytes_.resize(active);
    for (auto& r : runs_)
      r.end_and_present = (r.end() - head_) | (r.end_and_present & kPresent);
    head_ = 0;
  }
  std::vector<std::uint8_t> bytes_;
  std::vector<Run> runs_;
  std::size_t head_{};
};
}  // namespace universal_gnss_driver::detail
