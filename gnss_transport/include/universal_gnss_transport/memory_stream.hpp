#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "universal_gnss_transport/byte_stream.hpp"
#include "universal_gnss_transport/transport_metrics.hpp"

namespace universal_gnss_transport
{

class MemoryByteSource : public ByteSource
{
public:
  explicit MemoryByteSource(std::vector<std::uint8_t> input = {});

  ReadResult Read(std::uint8_t* destination, std::size_t capacity) override;
  bool IsOpen() const override;
  void Close() override;

  void Reset(std::vector<std::uint8_t> input);
  void InjectNextReadError(TransportError error);

  std::size_t remaining_bytes() const;
  const TransportMetrics& metrics() const;

private:
  std::vector<std::uint8_t> input_{};
  std::size_t read_offset_{0u};
  bool open_{true};
  TransportError next_read_error_{TransportError::kNone};
  TransportMetrics metrics_{};
};

class MemoryByteSink : public ByteSink
{
public:
  MemoryByteSink() = default;

  WriteResult Write(const std::uint8_t* data, std::size_t size) override;
  bool IsOpen() const override;
  void Close() override;

  void Clear();
  void InjectNextWriteError(TransportError error);

  const std::vector<std::uint8_t>& written_bytes() const;
  const TransportMetrics& metrics() const;

private:
  std::vector<std::uint8_t> output_{};
  bool open_{true};
  TransportError next_write_error_{TransportError::kNone};
  TransportMetrics metrics_{};
};

class MemoryByteDuplex : public ByteDuplex
{
public:
  explicit MemoryByteDuplex(std::vector<std::uint8_t> input = {});

  ReadResult Read(std::uint8_t* destination, std::size_t capacity) override;
  WriteResult Write(const std::uint8_t* data, std::size_t size) override;
  bool IsOpen() const override;
  void Close() override;

  void Reset(std::vector<std::uint8_t> input);
  void InjectNextReadError(TransportError error);
  void InjectNextWriteError(TransportError error);

  std::size_t remaining_bytes() const;
  const std::vector<std::uint8_t>& written_bytes() const;
  const TransportMetrics& metrics() const;

private:
  std::vector<std::uint8_t> input_{};
  std::vector<std::uint8_t> output_{};
  std::size_t read_offset_{0u};
  bool open_{true};
  TransportError next_read_error_{TransportError::kNone};
  TransportError next_write_error_{TransportError::kNone};
  TransportMetrics metrics_{};
};

}  // namespace universal_gnss_transport
