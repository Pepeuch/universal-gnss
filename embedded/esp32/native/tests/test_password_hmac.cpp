// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_password_hmac.hpp"
#include "ug_password_kdf.hpp"
#include <cassert>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <string>
using namespace ug::embedded;
namespace
{
unsigned calls{}, fail_at{}, active{}, yields{};
class Hash
{
public:
  ~Hash()
  {
    Reset();
  }
  bool Start()
  {
    if (++calls == fail_at)
      return false;
    ++active;
    live_ = true;
    return SHA256_Init(&state_) == 1;
  }
  bool Update(const std::uint8_t* bytes, std::size_t size)
  {
    return ++calls != fail_at && live_ && SHA256_Update(&state_, bytes, size) == 1;
  }
  bool Clone(const Hash& source)
  {
    if (++calls == fail_at || !source.live_)
      return false;
    state_ = source.state_;
    live_ = true;
    ++active;
    return true;
  }
  bool Finish(std::array<std::uint8_t, 32>& out)
  {
    return ++calls != fail_at && live_ && SHA256_Final(out.data(), &state_) == 1;
  }
  void Reset()
  {
    if (live_)
      --active;
    live_ = false;
    Wipe(&state_, sizeof(state_));
  }

private:
  SHA256_CTX state_{};
  bool live_{};
};
bool Mac(void* context,
         const std::uint8_t* data,
         std::size_t size,
         std::array<std::uint8_t, 32>& out)
{
  return static_cast<PasswordHmac<Hash>*>(context)->Compute(data, size, out);
}
void Equivalent(const std::string& password)
{
  assert(active == 0);
  PasswordHmac<Hash> mac;
  assert(mac.Initialize(password));
  for (const std::string data : {std::string{},
                                 std::string("salt\0\0\0\1", 8),
                                 std::string(32, '\xa5'),
                                 std::string(65, '\x81')})
  {
    std::array<std::uint8_t, 32> actual{}, expected{};
    unsigned size{};
    assert(HMAC(EVP_sha256(),
                password.data(),
                password.size(),
                reinterpret_cast<const unsigned char*>(data.data()),
                data.size(),
                expected.data(),
                &size) &&
           size == expected.size());
    assert(mac.Compute(reinterpret_cast<const std::uint8_t*>(data.data()), data.size(), actual));
    assert(actual == expected);
  }
  // Reinitialization must destroy the earlier keyed states.
  assert(mac.Initialize("different key"));
}
}  // namespace
int main()
{
  for (auto size : {0u, 1u, 8u, 63u, 64u, 65u, 128u, 257u})
    Equivalent(std::string(size, '\xe9'));
  assert(active == 0);
  PasswordRecord record{};
  for (unsigned i = 0; i < record.salt.size(); ++i)
    record.salt[i] = i;
  for (auto iterations : {1u, 2u, 1024u, WebAuth::kIterations})
  {
    PasswordHmac<Hash> mac;
    assert(mac.Initialize("password"));
    std::array<std::uint8_t, 32> actual{}, expected{};
    yields = 0;
    assert(PasswordKdf(record, iterations, Mac, &mac, [] { ++yields; }, actual));
    assert(PKCS5_PBKDF2_HMAC("password",
                             8,
                             record.salt.data(),
                             record.salt.size(),
                             iterations,
                             EVP_sha256(),
                             expected.size(),
                             expected.data()) == 1);
    assert(actual == expected && yields == (iterations - 1) / 1024);
  }
  assert(active == 0);
  // Inject every initialization/prehash/clone/update/finish failure. No keyed
  // states may remain live after failure, nor may a partial result escape.
  for (unsigned failure = 1; failure <= 13; ++failure)
  {
    calls = 0;
    fail_at = failure;
    PasswordHmac<Hash> mac;
    std::array<std::uint8_t, 32> out;
    out.fill(9);
    const bool ready = mac.Initialize(std::string(65, 'x'));
    if (ready)
      assert(!mac.Compute(record.salt.data(), record.salt.size(), out));
    assert(active == 0);
    assert(!mac.Compute(record.salt.data(), record.salt.size(), out));
    assert((out == std::array<std::uint8_t, 32>{}));
  }
  fail_at = 0;
  PasswordHmac<Hash> mac;
  assert(mac.Initialize("password"));
  std::array<std::uint8_t, 32> out{};
  assert(!mac.Compute(nullptr, 1, out));
  assert(active == 0);
}
