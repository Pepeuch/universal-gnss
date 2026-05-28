#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_transport/memory_stream.hpp"
#include "universal_gnss_transport/ring_buffer.hpp"
#include "universal_gnss_transport/transport_metrics.hpp"

namespace
{

using universal_gnss_transport::ByteRingBuffer;
using universal_gnss_transport::MemoryByteDuplex;
using universal_gnss_transport::MemoryByteSink;
using universal_gnss_transport::MemoryByteSource;
using universal_gnss_transport::TransportError;
using universal_gnss_transport::TransportStatus;

struct TestContext
{
  int failures{0};

  void Expect(const bool condition, const std::string& message)
  {
    if (!condition)
    {
      ++failures;
      std::cerr << "FAILED: " << message << '\n';
    }
  }
};

void TestMemorySourceReadsInOrder(TestContext& ctx)
{
  MemoryByteSource source({0x10u, 0x20u, 0x30u, 0x40u});
  std::array<std::uint8_t, 3> buffer{};

  const auto first = source.Read(buffer.data(), 2u);
  ctx.Expect(first.status == TransportStatus::kOk && first.bytes_read == 2u &&
                 buffer[0] == 0x10u && buffer[1] == 0x20u,
             "memory source should read the first bytes in order");
  ctx.Expect(source.remaining_bytes() == 2u &&
                 source.metrics().bytes_read == 2u &&
                 source.metrics().read_errors == 0u,
             "memory source should update remaining bytes and read metrics");

  const auto second = source.Read(buffer.data(), buffer.size());
  ctx.Expect(second.status == TransportStatus::kOk && second.bytes_read == 2u &&
                 buffer[0] == 0x30u && buffer[1] == 0x40u,
             "memory source should continue reading the remaining bytes in order");

  const auto eof = source.Read(buffer.data(), buffer.size());
  ctx.Expect(eof.status == TransportStatus::kEndOfStream &&
                 eof.bytes_read == 0u &&
                 eof.error == TransportError::kNone,
             "memory source should report EOF after its input is exhausted");
}

void TestMemorySinkWritesInOrder(TestContext& ctx)
{
  MemoryByteSink sink;
  const std::array<std::uint8_t, 2> first = {0xAAu, 0xBBu};
  const std::array<std::uint8_t, 1> second = {0xCCu};

  const auto first_write = sink.Write(first.data(), first.size());
  const auto second_write = sink.Write(second.data(), second.size());

  ctx.Expect(first_write.status == TransportStatus::kOk &&
                 second_write.status == TransportStatus::kOk &&
                 first_write.bytes_written == 2u &&
                 second_write.bytes_written == 1u,
             "memory sink should accept writes");
  ctx.Expect(sink.written_bytes() == std::vector<std::uint8_t>({0xAAu, 0xBBu, 0xCCu}),
             "memory sink should preserve write order");
  ctx.Expect(sink.metrics().bytes_written == 3u &&
                 sink.metrics().write_errors == 0u,
             "memory sink should update write metrics");
}

void TestErrorHandlingAndClosedBehavior(TestContext& ctx)
{
  MemoryByteSource source({0x01u});
  source.InjectNextReadError(TransportError::kReadFailure);
  std::array<std::uint8_t, 1> read_buffer{};

  const auto injected_read = source.Read(read_buffer.data(), read_buffer.size());
  ctx.Expect(injected_read.status == TransportStatus::kError &&
                 injected_read.error == TransportError::kReadFailure &&
                 source.metrics().read_errors == 1u &&
                 source.metrics().last_error == TransportError::kReadFailure,
             "memory source should surface injected read errors and update metrics");

  source.Close();
  const auto closed_read = source.Read(read_buffer.data(), read_buffer.size());
  ctx.Expect(closed_read.status == TransportStatus::kClosed &&
                 closed_read.error == TransportError::kClosed,
             "closed memory source should reject reads");

  MemoryByteSink sink;
  sink.InjectNextWriteError(TransportError::kWriteFailure);
  const std::array<std::uint8_t, 1> write_buffer = {0x55u};

  const auto injected_write = sink.Write(write_buffer.data(), write_buffer.size());
  ctx.Expect(injected_write.status == TransportStatus::kError &&
                 injected_write.error == TransportError::kWriteFailure &&
                 sink.metrics().write_errors == 1u &&
                 sink.metrics().last_error == TransportError::kWriteFailure,
             "memory sink should surface injected write errors and update metrics");

  sink.Close();
  const auto closed_write = sink.Write(write_buffer.data(), write_buffer.size());
  ctx.Expect(closed_write.status == TransportStatus::kClosed &&
                 closed_write.error == TransportError::kClosed,
             "closed memory sink should reject writes");
}

void TestMemoryDuplex(TestContext& ctx)
{
  MemoryByteDuplex duplex({0x11u, 0x22u});
  std::array<std::uint8_t, 2> read_buffer{};
  const auto read = duplex.Read(read_buffer.data(), read_buffer.size());
  const std::array<std::uint8_t, 2> write_buffer = {0x33u, 0x44u};
  const auto write = duplex.Write(write_buffer.data(), write_buffer.size());

  ctx.Expect(read.status == TransportStatus::kOk &&
                 read.bytes_read == 2u &&
                 read_buffer[0] == 0x11u &&
                 write.status == TransportStatus::kOk &&
                 write.bytes_written == 2u,
             "memory duplex should support both reading and writing");
  ctx.Expect(duplex.written_bytes() == std::vector<std::uint8_t>({0x33u, 0x44u}) &&
                 duplex.metrics().bytes_read == 2u &&
                 duplex.metrics().bytes_written == 2u,
             "memory duplex should track both directions in one metrics object");
}

void TestTransportMetricsHelpers(TestContext& ctx)
{
  universal_gnss_transport::TransportMetrics metrics;
  universal_gnss_transport::NoteReadBytes(metrics, 7u);
  universal_gnss_transport::NoteWrittenBytes(metrics, 9u);
  universal_gnss_transport::NoteReadError(metrics, TransportError::kReadFailure);
  universal_gnss_transport::NoteWriteError(metrics, TransportError::kWriteFailure);
  universal_gnss_transport::NoteReconnect(metrics);
  universal_gnss_transport::ClearLastTransportError(metrics);

  ctx.Expect(metrics.bytes_read == 7u &&
                 metrics.bytes_written == 9u &&
                 metrics.read_errors == 1u &&
                 metrics.write_errors == 1u &&
                 metrics.reconnect_count == 1u &&
                 metrics.last_error == TransportError::kNone,
             "transport metrics helpers should update counters consistently");
}

void TestRingBuffer(TestContext& ctx)
{
  ByteRingBuffer<3u> ring;
  ctx.Expect(ring.Push(0x01u) && ring.Push(0x02u) && ring.Push(0x03u) && ring.full(),
             "ring buffer should accept bytes until full");
  ctx.Expect(!ring.Push(0x04u) && ring.overflow_count() == 1u,
             "ring buffer should reject overflow and count it");

  const auto first = ring.Pop();
  const auto second = ring.Pop();
  ctx.Expect(first.has_value() && second.has_value() &&
                 *first == 0x01u && *second == 0x02u,
             "ring buffer should pop bytes in FIFO order");

  const std::array<std::uint8_t, 2> refill = {0x04u, 0x05u};
  const std::size_t pushed = ring.Push(refill.data(), refill.size());
  ctx.Expect(pushed == 2u && ring.full(),
             "ring buffer should accept more bytes after space is freed");

  std::array<std::uint8_t, 3> drained{};
  const std::size_t popped = ring.Pop(drained.data(), drained.size());
  ctx.Expect(popped == 3u &&
                 drained[0] == 0x03u &&
                 drained[1] == 0x04u &&
                 drained[2] == 0x05u &&
                 ring.empty(),
             "ring buffer should preserve FIFO ordering across wrap-around");
}

}  // namespace

int main()
{
  TestContext ctx;

  TestMemorySourceReadsInOrder(ctx);
  TestMemorySinkWritesInOrder(ctx);
  TestErrorHandlingAndClosedBehavior(ctx);
  TestMemoryDuplex(ctx);
  TestTransportMetricsHelpers(ctx);
  TestRingBuffer(ctx);

  if (ctx.failures != 0)
  {
    std::cerr << ctx.failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All gnss_transport foundation tests passed\n";
  return EXIT_SUCCESS;
}
