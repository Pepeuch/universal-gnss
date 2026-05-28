#include "universal_gnss_transport/memory_stream.hpp"

#include <algorithm>

namespace universal_gnss_transport
{

namespace
{

ReadResult MakeClosedReadResult(TransportMetrics& metrics)
{
  NoteReadError(metrics, TransportError::kClosed);
  return ReadResult{0u, TransportStatus::kClosed, TransportError::kClosed};
}

WriteResult MakeClosedWriteResult(TransportMetrics& metrics)
{
  NoteWriteError(metrics, TransportError::kClosed);
  return WriteResult{0u, TransportStatus::kClosed, TransportError::kClosed};
}

ReadResult MakeInvalidReadResult(TransportMetrics& metrics)
{
  NoteReadError(metrics, TransportError::kInvalidArgument);
  return ReadResult{0u, TransportStatus::kError, TransportError::kInvalidArgument};
}

WriteResult MakeInvalidWriteResult(TransportMetrics& metrics)
{
  NoteWriteError(metrics, TransportError::kInvalidArgument);
  return WriteResult{0u, TransportStatus::kError, TransportError::kInvalidArgument};
}

ReadResult MakeInjectedReadResult(TransportMetrics& metrics, const TransportError error)
{
  NoteReadError(metrics, error);
  return ReadResult{0u, TransportStatus::kError, error};
}

WriteResult MakeInjectedWriteResult(TransportMetrics& metrics, const TransportError error)
{
  NoteWriteError(metrics, error);
  return WriteResult{0u, TransportStatus::kError, error};
}

}  // namespace

MemoryByteSource::MemoryByteSource(std::vector<std::uint8_t> input)
    : input_(std::move(input))
{
}

ReadResult MemoryByteSource::Read(std::uint8_t* destination, const std::size_t capacity)
{
  if (!open_)
  {
    return MakeClosedReadResult(metrics_);
  }

  if (next_read_error_ != TransportError::kNone)
  {
    const TransportError error = next_read_error_;
    next_read_error_ = TransportError::kNone;
    return MakeInjectedReadResult(metrics_, error);
  }

  if (capacity == 0u)
  {
    return ReadResult{};
  }

  if (destination == nullptr)
  {
    return MakeInvalidReadResult(metrics_);
  }

  if (read_offset_ >= input_.size())
  {
    return ReadResult{0u, TransportStatus::kEndOfStream, TransportError::kNone};
  }

  const std::size_t available = input_.size() - read_offset_;
  const std::size_t bytes_to_copy = std::min(capacity, available);
  std::copy_n(input_.data() + static_cast<std::ptrdiff_t>(read_offset_),
              static_cast<std::ptrdiff_t>(bytes_to_copy),
              destination);
  read_offset_ += bytes_to_copy;
  NoteReadBytes(metrics_, bytes_to_copy);
  return ReadResult{bytes_to_copy, TransportStatus::kOk, TransportError::kNone};
}

bool MemoryByteSource::IsOpen() const
{
  return open_;
}

void MemoryByteSource::Close()
{
  open_ = false;
}

void MemoryByteSource::Reset(std::vector<std::uint8_t> input)
{
  input_ = std::move(input);
  read_offset_ = 0u;
  open_ = true;
  next_read_error_ = TransportError::kNone;
  metrics_ = TransportMetrics{};
}

void MemoryByteSource::InjectNextReadError(const TransportError error)
{
  next_read_error_ = error;
}

std::size_t MemoryByteSource::remaining_bytes() const
{
  return read_offset_ < input_.size() ? (input_.size() - read_offset_) : 0u;
}

const TransportMetrics& MemoryByteSource::metrics() const
{
  return metrics_;
}

WriteResult MemoryByteSink::Write(const std::uint8_t* data, const std::size_t size)
{
  if (!open_)
  {
    return MakeClosedWriteResult(metrics_);
  }

  if (next_write_error_ != TransportError::kNone)
  {
    const TransportError error = next_write_error_;
    next_write_error_ = TransportError::kNone;
    return MakeInjectedWriteResult(metrics_, error);
  }

  if (size == 0u)
  {
    return WriteResult{};
  }

  if (data == nullptr)
  {
    return MakeInvalidWriteResult(metrics_);
  }

  output_.insert(
      output_.end(),
      data,
      data + static_cast<std::ptrdiff_t>(size));
  NoteWrittenBytes(metrics_, size);
  return WriteResult{size, TransportStatus::kOk, TransportError::kNone};
}

bool MemoryByteSink::IsOpen() const
{
  return open_;
}

void MemoryByteSink::Close()
{
  open_ = false;
}

void MemoryByteSink::Clear()
{
  output_.clear();
  open_ = true;
  next_write_error_ = TransportError::kNone;
  metrics_ = TransportMetrics{};
}

void MemoryByteSink::InjectNextWriteError(const TransportError error)
{
  next_write_error_ = error;
}

const std::vector<std::uint8_t>& MemoryByteSink::written_bytes() const
{
  return output_;
}

const TransportMetrics& MemoryByteSink::metrics() const
{
  return metrics_;
}

MemoryByteDuplex::MemoryByteDuplex(std::vector<std::uint8_t> input)
    : input_(std::move(input))
{
}

ReadResult MemoryByteDuplex::Read(std::uint8_t* destination, const std::size_t capacity)
{
  if (!open_)
  {
    return MakeClosedReadResult(metrics_);
  }

  if (next_read_error_ != TransportError::kNone)
  {
    const TransportError error = next_read_error_;
    next_read_error_ = TransportError::kNone;
    return MakeInjectedReadResult(metrics_, error);
  }

  if (capacity == 0u)
  {
    return ReadResult{};
  }

  if (destination == nullptr)
  {
    return MakeInvalidReadResult(metrics_);
  }

  if (read_offset_ >= input_.size())
  {
    return ReadResult{0u, TransportStatus::kEndOfStream, TransportError::kNone};
  }

  const std::size_t available = input_.size() - read_offset_;
  const std::size_t bytes_to_copy = std::min(capacity, available);
  std::copy_n(input_.data() + static_cast<std::ptrdiff_t>(read_offset_),
              static_cast<std::ptrdiff_t>(bytes_to_copy),
              destination);
  read_offset_ += bytes_to_copy;
  NoteReadBytes(metrics_, bytes_to_copy);
  return ReadResult{bytes_to_copy, TransportStatus::kOk, TransportError::kNone};
}

WriteResult MemoryByteDuplex::Write(const std::uint8_t* data, const std::size_t size)
{
  if (!open_)
  {
    return MakeClosedWriteResult(metrics_);
  }

  if (next_write_error_ != TransportError::kNone)
  {
    const TransportError error = next_write_error_;
    next_write_error_ = TransportError::kNone;
    return MakeInjectedWriteResult(metrics_, error);
  }

  if (size == 0u)
  {
    return WriteResult{};
  }

  if (data == nullptr)
  {
    return MakeInvalidWriteResult(metrics_);
  }

  output_.insert(
      output_.end(),
      data,
      data + static_cast<std::ptrdiff_t>(size));
  NoteWrittenBytes(metrics_, size);
  return WriteResult{size, TransportStatus::kOk, TransportError::kNone};
}

bool MemoryByteDuplex::IsOpen() const
{
  return open_;
}

void MemoryByteDuplex::Close()
{
  open_ = false;
}

void MemoryByteDuplex::Reset(std::vector<std::uint8_t> input)
{
  input_ = std::move(input);
  output_.clear();
  read_offset_ = 0u;
  open_ = true;
  next_read_error_ = TransportError::kNone;
  next_write_error_ = TransportError::kNone;
  metrics_ = TransportMetrics{};
}

void MemoryByteDuplex::InjectNextReadError(const TransportError error)
{
  next_read_error_ = error;
}

void MemoryByteDuplex::InjectNextWriteError(const TransportError error)
{
  next_write_error_ = error;
}

std::size_t MemoryByteDuplex::remaining_bytes() const
{
  return read_offset_ < input_.size() ? (input_.size() - read_offset_) : 0u;
}

const std::vector<std::uint8_t>& MemoryByteDuplex::written_bytes() const
{
  return output_;
}

const TransportMetrics& MemoryByteDuplex::metrics() const
{
  return metrics_;
}

}  // namespace universal_gnss_transport
