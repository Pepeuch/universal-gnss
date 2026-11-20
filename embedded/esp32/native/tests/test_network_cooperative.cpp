// SPDX-License-Identifier: LGPL-3.0-only
#define UG_NETWORK_COOPERATIVE 1
#define UG_NETWORK_TEST_LIBRARY 1
#include "test_network_lifecycle.cpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#if UG_NETWORK_STACK_PSRAM
int xTaskCreateWithCaps(TaskFunction_t fn,
                        const char* name,
                        unsigned stack,
                        void* context,
                        unsigned priority,
                        void* handle,
                        unsigned caps)
{
  assert(fn == NetworkLifecycle && caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) && stack == 12288);
  return xTaskCreate(fn, name, stack, context, priority, handle);
}
#endif
int main()
{
  // Partial construction must clear raw inline storage as well as destroyed
  // C++ children before returning its explicitly allocated PSRAM block.
  struct AbortedOwner
  {
    unsigned char inline_bytes[32];
  };
  AbortedOwner* aborted{};
  const auto initial_payloads = payloads.size();
  assert(!ReserveOwner(aborted, [](void* memory) -> AbortedOwner* {
    std::memset(memory, 0x5a, sizeof(AbortedOwner));
    expect_zeroed_payload = memory;
    expect_zeroed_size = sizeof(AbortedOwner);
    throw std::bad_alloc{};
  }));
  assert(!aborted && payload_wipe_verified && payloads.size() == initial_payloads);
  payload_wipe_verified = false;
  allocations = 0;
  // Exact native service with synchronous RTOS seam, not an independent model.
  assert(StartNetworkServices() == ESP_OK && tasks.size() == 1);
  assert(tasks[0].stack == 12288 && !helper_live);
  assert(StartNetworkServices() == ESP_ERR_INVALID_STATE);
  NetworkAvailabilityChanged(true);
  TickManager();
  assert(allocations == 0 && tasks.size() == 1);
  config->publishers[0].enabled = true;
  config->publishers[0].endpoint.host = "127.0.0.1";
  config->publishers[0].endpoint.port = 1;
  config->publishers[0].endpoint.mountpoint = "TEST";
  config->publishers[0].endpoint.password = "test-only";
  no_memory = true;
  TickManager();
  assert(!cooperative_slots[1].endpoint && !helper_live);
  no_memory = false;
  TickManager();
  assert(!cooperative_slots[1].endpoint);  // First allocation failure backs off too.
  clock_us += 2000000;
  no_task = true;
  TickManager();
  auto& first = cooperative_slots[1];
  assert(first.endpoint && first.job.state() == ExclusiveJob::State::kOwner && !helper_live);
  assert(last_service_error == ESP_ERR_NO_MEM);
  no_task = false;
  clock_us += 2000000;
  TickManager();
  assert(helper_live && tasks.size() == 2 && tasks[1].stack == 10240);
  assert(first.job.state() == ExclusiveJob::State::kRequested);
  const auto held = allocations;
  assert(first.job.Take());
  const auto revision = first.endpoint->revision;
  // While endpoint0 is helper-owned (a blocked DNS/TLS operation), its sibling
  // can configure, retire and stop. The owner cannot close/reconfigure endpoint0.
  config->publishers[1] = config->publishers[0];
  ++config->revision;
  CooperativeTurn(1);
  assert(first.endpoint->revision == revision && first.endpoint->configured);
  CooperativeTurn(2);
  auto& second = cooperative_slots[2];
  assert(second.endpoint && second.job.state() == ExclusiveJob::State::kRequested);
  assert(second.job.Take());
  second.endpoint->error = ESP_ERR_NO_MEM;
  assert(second.job.Return());
  CooperativeTurn(2);
  assert(!second.endpoint->configured && last_service_error == ESP_ERR_NO_MEM);
  config->publishers[1].enabled = false;
  CooperativeTurn(2);
  assert(!second.endpoint->health);
  // Returning from a cancelled generation does not authorize a new callback
  // until the router has acknowledged the single withdrawal ticket.
  assert(first.job.Return());
  refuse_retire = retire_calls + 3;
  CooperativeTurn(1);
  assert(!first.endpoint->configured && first.endpoint->retiring);
  const auto ticket = first.endpoint->withdrawal;
  CooperativeTurn(1);
  assert(first.endpoint->withdrawal == ticket);
  refuse_retire = 0;
  CooperativeTurn(1);
  assert(!first.endpoint->retiring && first.endpoint->revision == config->revision);
  assert(first.job.Take());
  first.endpoint->error = ESP_OK;
  assert(first.job.Return());
  config->publishers[0].enabled = false;
  CooperativeTurn(1);
  assert(!first.endpoint->health && !first.endpoint->configured);
  const auto retained = first.endpoint;
  const auto allocations_before = allocations;
  for (unsigned i = 0; i < 100; ++i)
  {
    config->publishers[0].enabled = true;
    CooperativeTurn(1);
    assert(first.job.Take());
    first.endpoint->error = ESP_OK;
    assert(first.job.Return());
    config->publishers[0].enabled = false;
    CooperativeTurn(1);
    assert(first.endpoint == retained && !first.endpoint->configured);
  }
  assert(allocations == allocations_before && allocations > held);
  // Each of the seven requested services has its own owner/queue/filter context.
  config->upstream_enabled = true;
  config->caster_enabled = true;
  config->local_input = false;
  config->upstream = config->publishers[0].endpoint;
  config->upstream.send_gga = true;
  for (auto& p : config->publishers)
  {
    p = config->publishers[0];
    p.enabled = true;
  }
  clock_us += 2000000;  // The independently failed sibling's retry delay has elapsed.
  TickManager();
  for (const auto& slot : cooperative_slots)
    assert(slot.endpoint && slot.endpoint->configured);
  // Periodic GGA must not park an already streaming input behind a sibling's
  // blocked helper operation. Actual UG NtripClient reads a real local socket.
  auto& upstream = cooperative_slots[0];
  assert(upstream.job.CancelPending());
  int pair[2];
  const int listener = socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  socklen_t length = sizeof(address);
  assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);
  assert(listen(listener, 1) == 0);
  pair[0] = socket(AF_INET, SOCK_STREAM, 0);
  assert(pair[0] >= 0);
  assert(connect(pair[0], reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  pair[1] = accept(listener, nullptr, nullptr);
  assert(pair[1] >= 0);
  close(listener);
  auto& client = const_cast<universal_gnss_ntrip::NtripClient&>(input_owner->client());
  assert(client.AdoptConnectedSocket(pair[0]) == universal_gnss_ntrip::NtripClientError::kNone);
  assert(client.SendRequest(Now()) == universal_gnss_ntrip::NtripClientError::kNone);
  const char response[] = "ICY 200 OK\r\n\r\n";
  assert(send(pair[1], response, sizeof(response) - 1, 0) == sizeof(response) - 1);
  auto& blocked = cooperative_slots[1];
  assert(blocked.job.Take());
  CooperativeTurn(0);
  assert(upstream.job.state() == ExclusiveJob::State::kOwner);
  assert(upstream.endpoint->last_gga_ns == 0 && client.correction_flow_state().response_accepted);
  assert(blocked.job.Return());
  const auto retained_input = input_owner;
  EndpointFailure(*upstream.endpoint, ESP_FAIL);
  assert(input_owner == retained_input && !input_owner->enabled());
  expect_zeroed_payload = input_owner;
  expect_zeroed_size = sizeof(*input_owner);
  EndpointFailure(*upstream.endpoint, ESP_ERR_NO_MEM);
  assert(payload_wipe_verified);
  assert(!input_owner && !upstream.endpoint->configured);
  assert(std::find(payloads.begin(), payloads.end(), retained_input) == payloads.end());
  // Retry recreates the aborted input owner, rather than allocating new config
  // while the old monitor history still occupies the nearly exhausted arena.
  clock_us += 2000000;
  CooperativeTurn(0);
  assert(input_owner && upstream.endpoint->configured);
  NetworkAvailabilityChanged(false);
  for (auto& slot : cooperative_slots)
  {
    if (slot.job.Take())
    {
      slot.endpoint->error = ESP_OK;
      assert(slot.job.Return());
    }
  }
  TickManager();
  for (const auto& slot : cooperative_slots)
    assert(!slot.endpoint->configured && !slot.endpoint->health);
  close(pair[1]);
  // Test teardown only: callbacks/RTOS tasks are simulated and all quiesced.
  for (auto& slot : cooperative_slots)
    if (slot.endpoint)
      slot.endpoint->~CooperativeEndpoint();
  if (input_owner)
    input_owner->~NtripConnection();
  for (auto* p : publisher_owners)
    if (p)
      p->~NtripPublisher();
  if (caster_owner)
    caster_owner->~LocalRtcmCaster();
  for (auto* p : payloads)
    std::free(p);
}
