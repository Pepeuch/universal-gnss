// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace ug::embedded
{
struct PasswordRecord
{
  std::array<std::uint8_t, 16> salt{};
  std::array<std::uint8_t, 32> hash{};
};
enum class AuthRead
{
  kMissing,
  kFound,
  kError
};
class AuthBackend
{
public:
  virtual ~AuthBackend() = default;
  virtual AuthRead Read(PasswordRecord&) = 0;
  virtual bool Save(const PasswordRecord&) = 0;  // Atomic write + exact readback.
  virtual bool Random(std::uint8_t*, std::size_t) = 0;
  virtual bool Derive(std::string_view, const PasswordRecord&, std::array<std::uint8_t, 32>&) = 0;
  virtual bool Legacy(std::string_view password, std::string_view expected) = 0;
};
// HTTP owner only. Times are local monotonic microseconds; no GNSS clock involved.
class WebAuth
{
public:
  static constexpr unsigned kIterations = 600000;
  static constexpr std::int64_t kIdle = 600000000LL;
  explicit WebAuth(AuthBackend& backend) : backend_(backend)
  {
  }
  void Start(bool fresh, std::string_view legacy_hash);
  bool Login(std::string_view user,
             std::string_view password,
             std::int64_t now,
             std::array<char, 65>& token,
             bool& restricted);
  bool Authorized(std::string_view token, std::int64_t now, bool allow_restricted = false);
  bool
  Change(std::string_view token, std::string_view current, std::string_view next, std::int64_t now);
  void Logout(std::string_view token);
  bool LegacyAllowed() const
  {
    return ready_ && !configured_ && !legacy_.empty();
  }
  bool configured() const
  {
    return ready_ && configured_;
  }
  bool ready() const
  {
    return ready_;
  }

private:
  struct Session
  {
    std::array<char, 65> token{};
    std::int64_t created{}, used{};
    bool restricted{}, active{};
  };
  bool Verify(std::string_view);
  Session* Find(std::string_view, std::int64_t now);
  AuthBackend& backend_;
  PasswordRecord record_{};
  std::array<Session, 4> sessions_{};
  std::array<char, 65> legacy_buffer_{};
  std::string_view legacy_{};
  std::int64_t next_login_{-1};
  bool ready_{}, configured_{}, fresh_{};
};
void Wipe(void*, std::size_t);
}  // namespace ug::embedded
