#include "doctest.h"
#include "token.h"
#include <string>
#include <vector>

namespace {
std::vector<unsigned char> bytes(const std::string& s) {
  return std::vector<unsigned char>(s.begin(), s.end());
}
std::string hex(const std::vector<unsigned char>& v) {
  static const char* d = "0123456789abcdef";
  std::string out;
  for (unsigned char c : v) { out += d[c >> 4]; out += d[c & 0xf]; }
  return out;
}
}  // namespace

TEST_CASE("hmac_sha256 matches RFC 4231 test case 2") {
  auto mac = avc::hmac_sha256(bytes("Jefe"), bytes("what do ya want for nothing?"));
  CHECK(hex(mac) ==
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST_CASE("build_rtc_token returns an AccessToken2 string") {
  std::string t = avc::build_rtc_token(
      "0123456789abcdef0123456789abcdef",
      "0123456789abcdef0123456789abcdef",
      "room1", 1001, 3600);
  CHECK(t.rfind("007", 0) == 0);  // AccessToken2 version prefix
  CHECK(t.size() > 20);
}
