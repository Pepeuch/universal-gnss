#include "universal_gnss_ntrip/local_rtcm_caster.hpp"

#include "universal_gnss_ntrip/ntrip_request.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__) || defined(ESP_PLATFORM)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace universal_gnss_ntrip
{
namespace
{
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
constexpr char kIcyResponse[] = "ICY 200 OK\r\n\r\n";

std::string NormalizeMountpoint(std::string value)
{
  while (!value.empty() && value.front() == '/')
    value.erase(value.begin());
  return value;
}
}  // namespace

struct LocalRtcmCaster::Impl
{
  struct Client
  {
    int fd{-1};
    std::string request{};
    std::vector<std::uint8_t> pending{};
    std::size_t pending_offset{0u};
    bool ready{false};
    std::chrono::steady_clock::time_point accepted_at{std::chrono::steady_clock::now()};
  };
  LocalRtcmCasterConfig config{};
  LocalRtcmCasterPolicy policy{};
  LocalRtcmCasterMetrics metrics{};
  int listener{-1};
  std::uint16_t bound_port{0u};
  std::optional<LocalRtcmSourceIdentity> source{};
  std::optional<LocalRtcmSourceIdentity> previous_source{};
  std::vector<std::uint8_t> cached_1005{};
  std::vector<std::uint8_t> cached_1006{};
  std::vector<Client> clients{};
  std::string authorization{};
  void InvalidateClients()
  {
    if (!policy.disconnect_clients_on_source_change)
      return;
    for (auto& client : clients)
      Close(client);
    clients.clear();
  }
  bool Authorized(const std::string& request) const
  {
    if (authorization.empty())
      return true;
    std::size_t offset = request.find("\r\n");
    unsigned count = 0;
    bool accepted = false;
    while (offset != std::string::npos)
    {
      offset += 2;
      const auto end = request.find("\r\n", offset);
      if (end == std::string::npos || end == offset)
        break;
      const auto colon = request.find(':', offset);
      if (colon < end)
      {
        std::string key = request.substr(offset, colon - offset);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
          return static_cast<char>(std::tolower(c));
        });
        if (key == "authorization")
        {
          ++count;
          auto start = colon + 1;
          while (start < end && (request[start] == ' ' || request[start] == '\t'))
            ++start;
          const auto size = end - start;
          volatile unsigned difference = static_cast<unsigned>(size ^ authorization.size());
          for (std::size_t i = 0; i < authorization.size(); ++i)
            difference =
                difference | (static_cast<unsigned char>(authorization[i]) ^
                              static_cast<unsigned char>(i < size ? request[start + i] : 0));
          accepted = difference == 0;
        }
      }
      offset = end;
    }
    return count == 1 && accepted;
  }

  void Close(Client& client)
  {
#if defined(__linux__) || defined(ESP_PLATFORM)
    if (client.fd >= 0)
      ::close(client.fd);
#endif
    client.fd = -1;
  }
  void ClearCaches()
  {
    cached_1005.clear();
    cached_1006.clear();
  }
  bool Queue(Client& client, const std::vector<std::uint8_t>& bytes)
  {
    const std::size_t pending_bytes = client.pending.size() - client.pending_offset;
    if (bytes.size() > config.client_buffer_bytes - pending_bytes)
      return false;
    if (client.pending_offset == client.pending.size())
    {
      client.pending.clear();
      client.pending_offset = 0u;
    }
    else if (client.pending_offset >= 4096u && client.pending_offset * 2u >= client.pending.size())
    {
      client.pending.erase(client.pending.begin(), client.pending.begin() + client.pending_offset);
      client.pending_offset = 0u;
    }
    client.pending.insert(client.pending.end(), bytes.begin(), bytes.end());
    return true;
  }
  void DisconnectSlow(Client& client)
  {
    ++metrics.slow_client_disconnects;
    Close(client);
  }
};

LocalRtcmCaster::LocalRtcmCaster() : impl_(new Impl{})
{
}
LocalRtcmCaster::~LocalRtcmCaster()
{
  Stop();
  delete impl_;
}

bool LocalRtcmCaster::SetPolicy(LocalRtcmCasterPolicy policy)
{
  if (running() || policy.username.empty() != policy.password.empty())
    return false;
  impl_->authorization =
      policy.username.empty()
          ? ""
          : "Basic " + BuildBasicAuthorizationValue(policy.username, policy.password);
  impl_->policy = std::move(policy);
  return true;
}

bool LocalRtcmCaster::Start(LocalRtcmCasterConfig config)
{
  Stop();
  config.mountpoint = NormalizeMountpoint(std::move(config.mountpoint));
  if (config.mountpoint.empty() || config.client_buffer_bytes < sizeof(kIcyResponse))
    return false;
#if !defined(__linux__) && !defined(ESP_PLATFORM)
  (void)config;
  return false;
#else
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return false;
  const int reuse = 1;
  (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(config.port);
  if (::inet_pton(AF_INET, config.bind_host.c_str(), &address.sin_addr) != 1 ||
      ::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(fd, SOMAXCONN) != 0 || ::fcntl(fd, F_SETFL, O_NONBLOCK) != 0)
  {
    ::close(fd);
    return false;
  }
  socklen_t size = sizeof(address);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) != 0)
  {
    ::close(fd);
    return false;
  }
  impl_->config = std::move(config);
  impl_->listener = fd;
  impl_->bound_port = ntohs(address.sin_port);
  return true;
#endif
}

void LocalRtcmCaster::Stop()
{
#if defined(__linux__) || defined(ESP_PLATFORM)
  for (auto& client : impl_->clients)
    impl_->Close(client);
  if (impl_->listener >= 0)
    ::close(impl_->listener);
#endif
  impl_->clients.clear();
  impl_->listener = -1;
  impl_->bound_port = 0u;
  impl_->source.reset();
  impl_->ClearCaches();
}
bool LocalRtcmCaster::running() const
{
  return impl_->listener >= 0;
}
std::uint16_t LocalRtcmCaster::port() const
{
  return impl_->bound_port;
}

bool LocalRtcmCaster::ActivateSource(LocalRtcmSourceIdentity source)
{
  if (!running() || source.source_id.empty() || source.incarnation == 0u ||
      (impl_->previous_source.has_value() &&
       impl_->previous_source->source_id == source.source_id &&
       impl_->previous_source->incarnation == source.incarnation))
    return false;
  if (impl_->source.has_value())
    impl_->InvalidateClients();
  impl_->ClearCaches();
  impl_->source = std::move(source);
  impl_->previous_source = impl_->source;
  return true;
}
void LocalRtcmCaster::EndSource()
{
  if (impl_->source.has_value())
    impl_->InvalidateClients();
  impl_->source.reset();
  impl_->ClearCaches();
}
std::optional<LocalRtcmSourceIdentity> LocalRtcmCaster::active_source() const
{
  return impl_->source;
}

bool LocalRtcmCaster::PublishFrame(const universal_gnss_protocols::RtcmFrame& frame)
{
  if (!impl_->source.has_value() ||
      frame.checksum_status != universal_gnss_protocols::ChecksumStatus::kValid ||
      frame.raw_bytes.empty())
    return false;
  if (frame.message_type == 1005u)
    impl_->cached_1005 = frame.raw_bytes;
  if (frame.message_type == 1006u)
    impl_->cached_1006 = frame.raw_bytes;
  for (auto& client : impl_->clients)
    if (client.fd >= 0 && client.ready && !impl_->Queue(client, frame.raw_bytes))
      impl_->DisconnectSlow(client);
  ++impl_->metrics.served_frames;
  return true;
}

void LocalRtcmCaster::Poll()
{
#if defined(__linux__) || defined(ESP_PLATFORM)
  if (!running())
    return;
  std::size_t accepts = 0;
  while (impl_->policy.max_accepts_per_poll == 0 || accepts < impl_->policy.max_accepts_per_poll)
  {
    const int fd = ::accept(impl_->listener, nullptr, nullptr);
    if (fd < 0)
      break;
    ++accepts;
    if ((impl_->policy.max_clients && impl_->clients.size() >= impl_->policy.max_clients) ||
        ::fcntl(fd, F_SETFL, O_NONBLOCK) != 0)
    {
      ::close(fd);
      ++impl_->metrics.rejected_requests;
      continue;
    }
    impl_->clients.push_back({fd, {}, {}, 0u, false});
  }
  for (auto& client : impl_->clients)
  {
    if (client.fd < 0)
      continue;
    if (!client.ready)
    {
      if (impl_->policy.request_timeout_ms &&
          std::chrono::steady_clock::now() - client.accepted_at >=
              std::chrono::milliseconds(impl_->policy.request_timeout_ms))
      {
        ++impl_->metrics.rejected_requests;
        impl_->Close(client);
        continue;
      }
      char buffer[512];
      const ssize_t count = ::recv(client.fd, buffer, sizeof(buffer), 0);
      if (count > 0)
        client.request.append(buffer, static_cast<std::size_t>(count));
      if (count == 0 || client.request.size() > 2048u)
      {
        impl_->Close(client);
        continue;
      }
      const std::size_t line_end = client.request.find("\r\n");
      const bool complete =
          impl_->authorization.empty() || client.request.find("\r\n\r\n") != std::string::npos;
      if (line_end != std::string::npos && complete)
      {
        const std::string expected = "GET /" + impl_->config.mountpoint + " ";
        if (client.request.compare(0u, expected.size(), expected) != 0 ||
            !impl_->Authorized(client.request))
        {
          ++impl_->metrics.rejected_requests;
          impl_->Close(client);
          continue;
        }
        const std::vector<std::uint8_t> response(kIcyResponse,
                                                 kIcyResponse + sizeof(kIcyResponse) - 1u);
        if (!impl_->Queue(client, response) || !impl_->Queue(client, impl_->cached_1005) ||
            !impl_->Queue(client, impl_->cached_1006))
        {
          impl_->DisconnectSlow(client);
          continue;
        }
        client.ready = true;
        ++impl_->metrics.accepted_clients;
      }
    }
    if (client.fd >= 0 && client.pending_offset < client.pending.size())
    {
      const ssize_t count = ::send(client.fd,
                                   client.pending.data() + client.pending_offset,
                                   client.pending.size() - client.pending_offset,
                                   kSendFlags);
      if (count > 0)
      {
        client.pending_offset += static_cast<std::size_t>(count);
        if (client.pending_offset == client.pending.size())
        {
          client.pending.clear();
          client.pending_offset = 0u;
        }
      }
      else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        impl_->Close(client);
    }
  }
  impl_->clients.erase(std::remove_if(impl_->clients.begin(),
                                      impl_->clients.end(),
                                      [](const Impl::Client& c) { return c.fd < 0; }),
                       impl_->clients.end());
#endif
}
const LocalRtcmCasterMetrics& LocalRtcmCaster::metrics() const
{
  return impl_->metrics;
}
}  // namespace universal_gnss_ntrip
