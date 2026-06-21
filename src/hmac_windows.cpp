#include "token.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdexcept>
#include <string>

namespace avc {
namespace {
void check(NTSTATUS s, const char* what) {
  if (s != 0) throw std::runtime_error(std::string("BCrypt ") + what + " failed");
}
}  // namespace

std::vector<unsigned char> hmac_sha256(const std::vector<unsigned char>& key,
                                       const std::vector<unsigned char>& message) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  check(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG), "open");
  BCRYPT_HASH_HANDLE hash = nullptr;
  unsigned char dummy = 0;
  NTSTATUS s = BCryptCreateHash(
      alg, &hash, nullptr, 0,
      key.empty() ? &dummy : const_cast<PUCHAR>(key.data()),
      static_cast<ULONG>(key.size()), 0);
  if (s != 0) { BCryptCloseAlgorithmProvider(alg, 0); check(s, "createhash"); }
  if (!message.empty()) {
    s = BCryptHashData(hash, const_cast<PUCHAR>(message.data()),
                       static_cast<ULONG>(message.size()), 0);
    if (s != 0) { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
                  check(s, "hashdata"); }
  }
  std::vector<unsigned char> out(32);
  s = BCryptFinishHash(hash, out.data(), static_cast<ULONG>(out.size()), 0);
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  check(s, "finishhash");
  return out;
}
}  // namespace avc
