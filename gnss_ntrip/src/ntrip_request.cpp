#include "universal_gnss_ntrip/ntrip_request.hpp"

#include <sstream>

namespace universal_gnss_ntrip
{

namespace
{

std::string Base64Encode(std::string_view input)
{
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string encoded;
  encoded.reserve(((input.size() + 2u) / 3u) * 4u);

  std::size_t index = 0u;
  while (index + 3u <= input.size())
  {
    const std::uint32_t chunk =
        (static_cast<std::uint32_t>(static_cast<unsigned char>(input[index])) << 16u) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(input[index + 1u])) << 8u) |
        static_cast<std::uint32_t>(static_cast<unsigned char>(input[index + 2u]));
    encoded.push_back(kAlphabet[(chunk >> 18u) & 0x3Fu]);
    encoded.push_back(kAlphabet[(chunk >> 12u) & 0x3Fu]);
    encoded.push_back(kAlphabet[(chunk >> 6u) & 0x3Fu]);
    encoded.push_back(kAlphabet[chunk & 0x3Fu]);
    index += 3u;
  }

  const std::size_t remaining = input.size() - index;
  if (remaining == 1u)
  {
    const std::uint32_t chunk =
        static_cast<std::uint32_t>(static_cast<unsigned char>(input[index])) << 16u;
    encoded.push_back(kAlphabet[(chunk >> 18u) & 0x3Fu]);
    encoded.push_back(kAlphabet[(chunk >> 12u) & 0x3Fu]);
    encoded.push_back('=');
    encoded.push_back('=');
  }
  else if (remaining == 2u)
  {
    const std::uint32_t chunk =
        (static_cast<std::uint32_t>(static_cast<unsigned char>(input[index])) << 16u) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(input[index + 1u])) << 8u);
    encoded.push_back(kAlphabet[(chunk >> 18u) & 0x3Fu]);
    encoded.push_back(kAlphabet[(chunk >> 12u) & 0x3Fu]);
    encoded.push_back(kAlphabet[(chunk >> 6u) & 0x3Fu]);
    encoded.push_back('=');
  }

  return encoded;
}

std::string ResolveUserAgent(const std::string& user_agent)
{
  return user_agent.empty() ? std::string{kDefaultNtripUserAgent} : user_agent;
}

}  // namespace

std::string NormalizeMountpointPath(const std::string_view mountpoint)
{
  std::size_t index = 0u;
  while (index < mountpoint.size() && mountpoint[index] == '/')
  {
    ++index;
  }

  if (index >= mountpoint.size())
  {
    return "/";
  }

  return "/" + std::string(mountpoint.substr(index));
}

std::string BuildBasicAuthorizationValue(const std::string_view username,
                                         const std::string_view password)
{
  if (username.empty() && password.empty())
  {
    return {};
  }

  return "Basic " + Base64Encode(std::string(username) + ":" + std::string(password));
}

std::string BuildAuthorizationHeader(const std::string_view username,
                                     const std::string_view password)
{
  const std::string value = BuildBasicAuthorizationValue(username, password);
  if (value.empty())
  {
    return {};
  }

  return "Authorization: " + value + "\r\n";
}

NtripRequest BuildNtripGetRequest(const NtripConfig& config)
{
  NtripRequest request;
  request.mountpoint_path = NormalizeMountpointPath(config.mountpoint);
  request.includes_authorization =
      !(config.username.empty() && config.password.empty());
  request.includes_ntrip_version_header = config.version == NtripVersion::kV2;

  const std::string user_agent = ResolveUserAgent(config.user_agent);

  std::ostringstream output;
  output << "GET " << request.mountpoint_path << ' ';
  if (config.version == NtripVersion::kV1)
  {
    output << "HTTP/1.0\r\n";
  }
  else
  {
    output << "HTTP/1.1\r\n";
  }

  if (config.version == NtripVersion::kV2)
  {
    output << "Host: " << config.host << ':' << config.port << "\r\n";
  }

  output << "User-Agent: NTRIP " << user_agent << "\r\n"
         << "Accept: */*\r\n"
         << "Connection: close\r\n";

  if (config.version == NtripVersion::kV2)
  {
    output << "Ntrip-Version: Ntrip/2.0\r\n";
  }

  output << BuildAuthorizationHeader(config.username, config.password);
  output << "\r\n";

  request.request_text = output.str();
  return request;
}

}  // namespace universal_gnss_ntrip
