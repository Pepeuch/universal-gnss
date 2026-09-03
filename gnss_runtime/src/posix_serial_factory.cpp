#include "universal_gnss_runtime/posix_serial_factory.hpp"

#include <memory>
#include <utility>

namespace universal_gnss_runtime {
namespace {

const char* ToString(const universal_gnss_transport::TransportError error)
{
  using universal_gnss_transport::TransportError;
  switch (error)
  {
  case TransportError::kNone:
    return "none";
  case TransportError::kClosed:
    return "closed";
  case TransportError::kInvalidArgument:
    return "invalid_argument";
  case TransportError::kOverflow:
    return "overflow";
  case TransportError::kConnectFailure:
    return "connect_failure";
  case TransportError::kTimeout:
    return "timeout";
  case TransportError::kReadFailure:
    return "read_failure";
  case TransportError::kWriteFailure:
    return "write_failure";
  case TransportError::kUnsupported:
    return "unsupported";
  case TransportError::kUnknown:
    return "unknown";
  case TransportError::kTlsHandshakeFailure:
    return "tls_handshake_failure";
  case TransportError::kTlsVerificationFailure:
    return "tls_verification_failure";
  }
  return "unknown";
}

} // namespace

TransportFactory MakePosixSerialTransportFactory(universal_gnss_transport::PosixSerialConfig config)
{
  return [config = std::move(config)]() {
    auto transport = std::make_unique<universal_gnss_transport::PosixSerialTransport>();
    const auto error = transport->Open(config);
    if (error != universal_gnss_transport::TransportError::kNone)
    {
      return TransportFactoryResult{nullptr, std::string("open:") + ToString(error)};
    }
    return TransportFactoryResult{std::move(transport), {}};
  };
}

} // namespace universal_gnss_runtime
