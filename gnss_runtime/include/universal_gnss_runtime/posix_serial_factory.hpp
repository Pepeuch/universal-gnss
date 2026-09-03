#pragma once

#include "universal_gnss_runtime/receiver_supervisor.hpp"
#include "universal_gnss_transport/posix_serial_transport.hpp"

namespace universal_gnss_runtime {

// Creates a factory for one explicitly configured serial receiver. It performs
// no probing or device discovery.
TransportFactory
MakePosixSerialTransportFactory(universal_gnss_transport::PosixSerialConfig config);

} // namespace universal_gnss_runtime
