// SPDX-License-Identifier: LGPL-3.0-only
// Included inside the native service's anonymous namespace. No shared-core fork.
// Only the I/O owner configures/retires an endpoint; a helper temporarily owns
// exactly one connection for DNS/connect/request/GGA, never its siblings.
enum class BlockingOperation
{
  kTick,
  kGga,
  kCasterStart
};
ServiceId EndpointService(std::size_t index)
{
  return index == 0 ? ServiceId::kNtripInput
         : index == 6
             ? ServiceId::kCaster
             : static_cast<ServiceId>(static_cast<unsigned>(ServiceId::kPublisher0) + index - 1);
}
struct CooperativeEndpoint
{
  explicit CooperativeEndpoint(std::size_t i) : index(i), trace{EndpointService(i), revision, link}
  {
  }
  std::size_t index;
  std::uint64_t revision{}, link{}, withdrawal{}, incarnation{};
  std::size_t source{Gateway::kSources};
  std::int64_t retry_ns{}, last_gga_ns{};
  std::optional<WorkerHealth> health;
  TraceContext trace;
  Gateway::Frame frame{};
  NtripPublisher::PendingFrame pending;
  BlockingOperation operation{BlockingOperation::kTick};
  std::int32_t error{};
  bool configured{}, caster_started{}, retiring{};
};
struct CooperativeControl
{
  ExclusiveJob job;  // Atomic/control remains internal; endpoint payload is PSRAM.
  CooperativeEndpoint* endpoint{};
  std::int64_t allocation_retry_ns{};  // I/O owner; bounds first-owner allocation failures.
};
std::array<CooperativeControl, 7> cooperative_slots;
std::atomic<bool> helper_live{false};
std::int64_t helper_retry_ns{};  // I/O owner only; do not spin on a failed task allocation.

void CloseEndpoint(CooperativeEndpoint& e)
{
  if (e.index == 0 && input_owner)
  {
    input_owner->Stop();
    upstream_generation = BeginCorrectionSource(kUpstream, false);
  }
  else if (e.index > 0 && e.index <= UG_SERVICE_PUBLISHERS && publisher_owners[e.index - 1])
    publisher_owners[e.index - 1]->Stop();
#if UG_SERVICE_CASTER
  else if (e.index == 6 && caster_owner)
    caster_owner->Stop();
#endif
  e.pending.active = false;
  e.caster_started = false;
  e.configured = false;
  e.source = Gateway::kSources;
  e.incarnation = 0;
  if (e.index && !e.retiring)
  {
    e.withdrawal = WithdrawCorrectionDestination(e.index);
    e.retiring = true;
  }
}
bool Retired(CooperativeEndpoint& e)
{
  if (!e.retiring)
    return true;
  if (!e.withdrawal || !CorrectionDestinationRetired(e.index, e.withdrawal))
  {
    ObserveService(EndpointService(e.index), ServiceState::kBlocked, ESP_ERR_TIMEOUT);
    return false;
  }
  if (e.index <= UG_SERVICE_PUBLISHERS && publishers[e.index - 1].queue)
    xQueueReset(publishers[e.index - 1].queue);
#if UG_SERVICE_CASTER
  else if (e.index == 6 && caster_box.queue)
    xQueueReset(caster_box.queue);
#endif
  e.retiring = false;
  e.withdrawal = 0;
  return true;
}
void EndpointFailure(CooperativeEndpoint& e, std::int32_t error)
{
  CloseEndpoint(e);
  if (error == ESP_ERR_NO_MEM && e.index == 0 && input_owner)
  {
    // Stop invalidates the source and closes its socket, but intentionally keeps
    // ordinary reconnect history. An aborted OOM session must release that
    // history before Configure/Connect can allocate again. The helper has
    // returned exclusive ownership before this path; no router uses this object.
    input_owner->SetTrace({});
    input_owner->~NtripConnection();
    // Inline string storage can still hold credentials after member destruction.
    mbedtls_platform_zeroize(input_owner, sizeof(*input_owner));
    heap_caps_free(input_owner);
    input_owner = nullptr;
  }
  e.retry_ns = Now() + 1000000000LL;
  ObserveService(EndpointService(e.index), ServiceState::kFailed, error);
}
bool ConfigureEndpoint(CooperativeEndpoint& e)
{
  const auto config = Configuration();
  e.revision = config->revision;
  e.link = network_generation.load();
  e.trace.attempt = 0;
  auto cancelled = [revision = e.revision, link = e.link] {
    return !network_ready.load() || network_generation.load() != link ||
           Configuration()->revision != revision;
  };
  if (e.index == 0)
  {
    if (!ReserveOwner(input_owner, [](void* memory) {
          return new (memory) NtripConnection(
              [](const auto& frame) {
                const auto epoch = upstream_generation.load();
                if (epoch && frame.timestamp_ns)
                  SubmitCorrectionBytes(kUpstream,
                                        epoch,
                                        frame.raw_bytes.data(),
                                        frame.raw_bytes.size(),
                                        *frame.timestamp_ns,
                                        true);
              },
              [](bool active) {
                upstream_generation =
                    BeginCorrectionSource(kUpstream, active && network_ready.load());
              });
        }))
      return false;
    input_owner->SetTrace(e.trace.Sink());
    if (!input_owner->Configure(config->upstream, true, cancelled, true))
      return false;
  }
  else if (e.index <= UG_SERVICE_PUBLISHERS)
  {
    const auto slot = e.index - 1;
    auto& box = publishers[slot];
    box.slot = slot;
    if (!ReserveOwner(publisher_owners[slot],
                      [](void* memory) { return new (memory) NtripPublisher{}; }) ||
        (!box.queue && !box.Create()))
      return false;
    auto& owner = *publisher_owners[slot];
    owner.SetTrace(e.trace.Sink());
    if (!owner.Configure(config->publishers[slot].endpoint,
                         true,
                         cancelled,
                         config->publishers[slot].rtcm_messages,
                         true))
      return false;
    SetCorrectionDestination(e.index, Outbox::Enqueue, &box);
  }
#if UG_SERVICE_CASTER
  else if (e.index == 6)
  {
    if (!ReserveOwner(
            caster_owner,
            [](void* memory) { return new (memory) universal_gnss_ntrip::LocalRtcmCaster{}; }) ||
        (!caster_box.queue && !caster_box.Create()))
      return false;
  }
#endif
  if (!e.health)
  {
    ExpectWorker(EndpointService(e.index), 30000000000LL);
    e.health.emplace(EndpointService(e.index), 30000000000LL, false);
  }
  e.configured = true;
  ObserveService(EndpointService(e.index), ServiceState::kRunning);
  return true;
}
void BlockingStep(CooperativeEndpoint& e)
{
  NetworkAllocationScope payload(static_cast<unsigned>(e.index) + 1);
  if (!EndpointRequested(e.index) || e.revision != Configuration()->revision ||
      e.link != network_generation.load())
    return;
  IoExecutionScope blocking(IoExecution::kBlocking);
  if (e.index == 0)
  {
    if (e.operation == BlockingOperation::kGga)
    {
      const auto position = GnssObservation();
      const auto now = Now();
      const auto fresh = FreshPositionForGga(position.state, position.position_receipt_ns, now);
      input_owner->InjectGga(now, fresh);
    }
    else
      input_owner->Tick(Now(), nullptr, false);
  }
  else if (e.index <= UG_SERVICE_PUBLISHERS)
    publisher_owners[e.index - 1]->Tick(Now());
#if UG_SERVICE_CASTER
  else if (e.index == 6)
  {
    const auto config = Configuration();
    universal_gnss_ntrip::LocalRtcmCasterPolicy policy;
    policy.max_clients = kCasterClientLimit;
    policy.max_accepts_per_poll = 2;
    policy.request_timeout_ms = 5000;
    policy.disconnect_clients_on_source_change = true;
    policy.username = config->caster_user;
    policy.password = config->caster_password;
    universal_gnss_ntrip::LocalRtcmCasterConfig cfg;
    cfg.bind_host = "0.0.0.0";
    cfg.port = config->caster_port;
    cfg.mountpoint = config->caster_mountpoint;
    cfg.client_buffer_bytes = 4096;
    e.caster_started = caster_owner->SetPolicy(policy) && caster_owner->Start(cfg);
    if (!e.caster_started)
      e.error = ESP_FAIL;
  }
#endif
}
// Xtensa stops exception search at the zero-return FreeRTOS entry frame.
// Keep the handler in a real callee; the task continues after this call.
__attribute__((noinline)) void GuardedBlockingStep(CooperativeEndpoint& e, std::size_t index)
{
  try
  {
    BlockingStep(e);
  }
  catch (const std::bad_alloc&)
  {
#if UG_NETWORK_ALLOCATION_TEST
    std::printf("UG_MEM helper_catch=%u kind=bad_alloc\n", unsigned(index));
#endif
    e.error = ESP_ERR_NO_MEM;
  }
  catch (...)
  {
#if UG_NETWORK_ALLOCATION_TEST
    std::printf("UG_MEM helper_catch=%u kind=other\n", unsigned(index));
#endif
    e.error = ESP_FAIL;
  }
}
void CooperativeHelper(void*)
{
#if defined(ESP_PLATFORM)
  std::printf("UG_CPU role=network_helper profile=%d configured_core=%d observed_core=%d\n",
              UG_S3_CPU_PROFILE,
              ug_s3_worker_core(UG_S3_WORKER_NETWORK),
              xPortGetCoreID());
#endif
  std::size_t cursor = 0;
  auto last_work = Now();
  for (;;)
  {
    bool work = false;
    for (std::size_t n = 0; n < cooperative_slots.size(); ++n)
    {
      const auto index = (cursor + n) % cooperative_slots.size();
      auto& control = cooperative_slots[index];
      if (!control.job.Take())
        continue;
      auto& e = *control.endpoint;
      e.error = ESP_OK;
      GuardedBlockingStep(e, index);
      (void)control.job.Return();  // Publication is the last access to this payload.
      cursor = (index + 1) % cooperative_slots.size();
      work = true;
      last_work = Now();
      break;
    }
    if (!work)
    {
      if (Now() - last_work >= 500000000LL)
        break;
      vTaskDelay(FiniteWaitTicks(5, configTICK_RATE_HZ));
    }
  }
  helper_live.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}
bool EnsureHelper()
{
  bool expected = false;
  if (Now() < helper_retry_ns)
    return false;
  if (!helper_live.compare_exchange_strong(expected, true))
    return true;
#if UG_S3_CPU_PROFILE > 0
  const bool started = xTaskCreatePinnedToCore(CooperativeHelper,
                                               "ug_network_open",
                                               10240,
                                               nullptr,
                                               2,
                                               nullptr,
                                               ug_s3_worker_core(UG_S3_WORKER_NETWORK)) == pdPASS;
#else
  const bool started =
      xTaskCreate(CooperativeHelper, "ug_network_open", 10240, nullptr, 2, nullptr) == pdPASS;
#endif
  if (started)
    return true;
  helper_live.store(false, std::memory_order_release);
  helper_retry_ns = Now() + 1000000000LL;
  return false;
}
void SampleEndpoint(CooperativeEndpoint& e)
{
  NetworkOwnerStats sample;
  sample.configuration_revision = e.revision;
  sample.link_generation = e.link;
  sample.receipt_ns = Now();
  sample.sampled = true;
  if (e.index == 0)
  {
    const auto& m = input_owner->client().metrics();
    sample.active = input_owner->client().IsConnected();
    sample.frames = m.rtcm_frames_received;
    sample.bytes = m.bytes_received;
    sample.connections = m.reconnect_count;
    const auto flowing = input_owner->client().IsCorrectionFlowing();
    portENTER_CRITICAL(&metrics_guard);
    stats.upstream = sample;
    stats.upstream_flowing = flowing;
    stats.upstream_error = static_cast<std::uint32_t>(m.last_error);
    stats.upstream_gga_sent = m.gga_sent_count;
    stats.upstream_gga_errors = m.gga_send_errors;
    portEXIT_CRITICAL(&metrics_guard);
  }
  else if (e.index <= UG_SERVICE_PUBLISHERS)
  {
    const auto& m = publisher_owners[e.index - 1]->metrics();
    sample.active = publisher_owners[e.index - 1]->streaming();
    sample.frames = m.frames;
    sample.bytes = m.bytes;
    sample.connections = m.connects;
    sample.failures = m.failures;
    sample.expired = m.expired;
    portENTER_CRITICAL(&metrics_guard);
    stats.publishers[e.index - 1] = sample;
    portEXIT_CRITICAL(&metrics_guard);
  }
#if UG_SERVICE_CASTER
  else
  {
    const auto& m = caster_owner->metrics();
    sample.active = caster_owner->running();
    sample.frames = m.served_frames;
    sample.connections = m.accepted_clients;
    sample.failures = m.rejected_requests;
    sample.expired = m.slow_client_disconnects;
    portENTER_CRITICAL(&metrics_guard);
    stats.caster = sample;
    portEXIT_CRITICAL(&metrics_guard);
  }
#endif
}
void ProgressEndpoint(CooperativeEndpoint& e, std::int64_t step_ns)
{
  IoExecutionScope nonblocking(IoExecution::kNonblocking);
  if (e.index == 0)
    input_owner->Tick(step_ns, nullptr, false);
  else if (e.index <= UG_SERVICE_PUBLISHERS)
  {
    auto& owner = *publisher_owners[e.index - 1];
    if ((e.source < Gateway::kSources && !CorrectionSourceCurrent(e.source, e.incarnation)) ||
        (e.pending.active && !Current(e.pending.frame)))
    {
      CloseEndpoint(e);
      return;
    }
    owner.Tick(step_ns);
    if (e.pending.active)
      owner.ProgressFrame(e.pending, Now());
    if (!e.pending.active)
    {
      // At most the fixed two-slot outbox per turn; never drain an unbounded producer.
      for (std::size_t n = 0; n < kQueueDepth && !e.pending.active; ++n)
      {
        if (xQueueReceive(publishers[e.index - 1].queue, &e.frame, 0) != pdTRUE)
          break;
        if (!Current(e.frame) || !owner.streaming())
          continue;
        if (e.source == Gateway::kSources)
        {
          e.source = e.frame.source;
          e.incarnation = e.frame.incarnation;
        }
        if (e.source != e.frame.source || e.incarnation != e.frame.incarnation)
        {
          CloseEndpoint(e);
          return;
        }
        (void)owner.BeginFrame(e.pending, e.frame, Now());
      }
    }
  }
#if UG_SERVICE_CASTER
  else if (e.caster_started)
  {
    if (e.source < Gateway::kSources && !CorrectionSourceCurrent(e.source, e.incarnation))
    {
      caster_owner->EndSource();
      e.source = Gateway::kSources;
      e.incarnation = 0;
      xQueueReset(caster_box.queue);
    }
    for (std::size_t n = 0; n < kQueueDepth; ++n)
    {
      if (xQueueReceive(caster_box.queue, &e.frame, 0) != pdTRUE)
        break;
      if (!Current(e.frame))
        continue;
      if (e.source != e.frame.source || e.incarnation != e.frame.incarnation)
      {
        caster_owner->EndSource();
        e.source = e.frame.source;
        e.incarnation = e.frame.incarnation;
        if (caster_cache_generation == UINT64_MAX ||
            !caster_owner->ActivateSource({"ug-selected-correction", ++caster_cache_generation}))
          continue;
      }
      universal_gnss_protocols::RtcmFrame frame;
      frame.raw_bytes.assign(e.frame.bytes.begin(), e.frame.bytes.begin() + e.frame.size);
      frame.checksum_status = universal_gnss_protocols::ChecksumStatus::kValid;
      frame.timestamp_ns = e.frame.receipt_ns;
      if (e.frame.size >= 8)
        frame.message_type = (std::uint16_t(e.frame.bytes[3]) << 4) | (e.frame.bytes[4] >> 4);
      (void)caster_owner->PublishFrame(frame);
    }
    caster_owner->Poll();
  }
#endif
  SampleEndpoint(e);
}
void CooperativeTurn(std::size_t index)
{
  NetworkAllocationScope payload(static_cast<unsigned>(index) + 1);
  auto& control = cooperative_slots[index];
  if (control.job.state() == ExclusiveJob::State::kRequested && control.endpoint &&
      (!EndpointRequested(index) || control.endpoint->revision != Configuration()->revision ||
       control.endpoint->link != network_generation.load()))
    (void)control.job.CancelPending();
  const bool returned = control.job.Reclaim();
  if (control.job.state() != ExclusiveJob::State::kOwner)
    return;
  const bool requested = EndpointRequested(index);
  if (!control.endpoint)
  {
    if (!requested || Now() < control.allocation_retry_ns)
      return;
    if (!ReserveOwner(control.endpoint,
                      [index](void* memory) { return new (memory) CooperativeEndpoint(index); }))
    {
      control.allocation_retry_ns = Now() + 1000000000LL;
      ObserveService(EndpointService(index), ServiceState::kFailed, ESP_ERR_NO_MEM);
      return;
    }
  }
  auto& e = *control.endpoint;
  if (returned && e.error != ESP_OK)
    EndpointFailure(e, e.error);
  if (e.health)
    e.health->Progress();
  if (e.configured && (!requested || e.revision != Configuration()->revision ||
                       e.link != network_generation.load()))
    CloseEndpoint(e);
  if (!Retired(e))
    return;
  if (!requested)
  {
    if (e.health)
    {
      e.health->Finish();
      e.health.reset();
    }
    ObserveService(EndpointService(index), ServiceState::kDisabled);
    return;
  }
  if (Now() < e.retry_ns)
    return;
  if (!e.configured && !ConfigureEndpoint(e))
  {
    EndpointFailure(e, ESP_ERR_NO_MEM);
    return;
  }
  if (index == 6 && e.caster_started && returned && e.operation == BlockingOperation::kCasterStart)
    SetCorrectionDestination(6, Outbox::Enqueue, &caster_box);
  bool blocking = false;
  // Use one time for the due decision and Tick. Crossing a backoff deadline
  // between two clock reads must not accidentally start Open on the I/O owner.
  const auto step_ns = Now();
  e.operation = BlockingOperation::kTick;
  if (index == 0)
  {
    blocking = input_owner->BlockingStepDue(step_ns);
    if (!blocking && input_owner->client().IsConnected() &&
        input_owner->client().config().send_gga && Now() - e.last_gga_ns >= 1000000000LL)
    {
      // Do not park a streaming input behind another endpoint's DNS/handshake
      // merely to send periodic GGA. Defer GGA while continuing normal reads.
      const bool helper_idle =
          std::none_of(cooperative_slots.begin(), cooperative_slots.end(), [](const auto& slot) {
            const auto state = slot.job.state();
            return state == ExclusiveJob::State::kRequested ||
                   state == ExclusiveJob::State::kExecuting;
          });
      if (helper_idle)
      {
        e.operation = BlockingOperation::kGga;
        e.last_gga_ns = Now();
        blocking = true;
      }
    }
  }
  else if (index <= UG_SERVICE_PUBLISHERS)
    blocking = publisher_owners[index - 1]->BlockingStepDue(step_ns);
#if UG_SERVICE_CASTER
  else if (!e.caster_started)
  {
    blocking = true;
    e.operation = BlockingOperation::kCasterStart;
  }
#endif
  if (blocking)
  {
    if (!EnsureHelper())
    {
      EndpointFailure(e, ESP_ERR_NO_MEM);
      return;
    }
    (void)control.job.Request();
    return;
  }
  ProgressEndpoint(e, step_ns);
}
// Do not inline this catch into NetworkLifecycle's bottom task frame.
__attribute__((noinline)) void GuardedCooperativeTurn(std::size_t index)
{
  try
  {
    CooperativeTurn(index);
  }
  catch (const std::bad_alloc&)
  {
#if UG_NETWORK_ALLOCATION_TEST
    std::printf("UG_MEM owner_catch=%u kind=bad_alloc\n", unsigned(index));
#endif
    if (cooperative_slots[index].endpoint)
      EndpointFailure(*cooperative_slots[index].endpoint, ESP_ERR_NO_MEM);
  }
  catch (...)
  {
#if UG_NETWORK_ALLOCATION_TEST
    std::printf("UG_MEM owner_catch=%u kind=other\n", unsigned(index));
#endif
    if (cooperative_slots[index].endpoint)
      EndpointFailure(*cooperative_slots[index].endpoint, ESP_FAIL);
  }
}
void NetworkLifecycle(void*)
{
#if defined(ESP_PLATFORM)
  std::printf("UG_CPU role=network_io profile=%d configured_core=%d observed_core=%d\n",
              UG_S3_CPU_PROFILE,
              ug_s3_worker_core(UG_S3_WORKER_NETWORK),
              xPortGetCoreID());
#endif
  WorkerHealth manager(ServiceId::kNtripSetup, 5000000000LL, false);
  for (;;)
  {
    for (std::size_t i = 0; i < cooperative_slots.size(); ++i)
    {
      GuardedCooperativeTurn(i);
    }
    // Recover the race where the last helper exits just as a new job is published.
    for (auto& control : cooperative_slots)
      if (control.job.state() == ExclusiveJob::State::kRequested && !helper_live.load() &&
          !EnsureHelper() && control.job.CancelPending())
        EndpointFailure(*control.endpoint, ESP_ERR_NO_MEM);
    manager.Progress();
    vTaskDelay(FiniteWaitTicks(1, configTICK_RATE_HZ));
  }
}
