#pragma once

#include <cstddef>
#include <cstdint>

#include "universal_gnss_transport/transport_error.hpp"
#include "universal_gnss_transport/transport_status.hpp"

namespace universal_gnss_transport
{

struct ReadResult
{
  std::size_t bytes_read{0u};
  TransportStatus status{TransportStatus::kOk};
  TransportError error{TransportError::kNone};
};

struct WriteResult
{
  std::size_t bytes_written{0u};
  TransportStatus status{TransportStatus::kOk};
  TransportError error{TransportError::kNone};
};

class ByteSource
{
public:
  virtual ~ByteSource() = default;

  virtual ReadResult Read(std::uint8_t* destination, std::size_t capacity) = 0;
  virtual bool IsOpen() const = 0;
  virtual void Close() = 0;
};

class ByteSink
{
public:
  virtual ~ByteSink() = default;

  virtual WriteResult Write(const std::uint8_t* data, std::size_t size) = 0;
  virtual bool IsOpen() const = 0;
  virtual void Close() = 0;
};

class ByteDuplex : public ByteSource, public ByteSink
{
public:
  ~ByteDuplex() override = default;
};

}  // namespace universal_gnss_transport
