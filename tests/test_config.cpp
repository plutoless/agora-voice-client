#include "doctest.h"
#include "config.h"
#include <stdexcept>

using avc::parse_config;
using avc::TokenSource;

TEST_CASE("missing app id throws") {
  CHECK_THROWS_AS(parse_config({}, {"--channel", "room1"}), std::runtime_error);
}

TEST_CASE("missing channel throws") {
  CHECK_THROWS_AS(parse_config({{"AGORA_APP_ID", "app"}}, {}), std::runtime_error);
}

TEST_CASE("explicit token wins") {
  auto c = parse_config({{"AGORA_APP_ID", "app"}, {"AGORA_TOKEN", "tok"},
                         {"AGORA_APP_CERTIFICATE", "cert"}},
                        {"--channel", "room1"});
  CHECK(c.token_source == TokenSource::Explicit);
  CHECK(c.explicit_token == "tok");
}

TEST_CASE("certificate without token means mint") {
  auto c = parse_config({{"AGORA_APP_ID", "app"}, {"AGORA_APP_CERTIFICATE", "cert"}},
                        {"--channel", "room1"});
  CHECK(c.token_source == TokenSource::Mint);
  CHECK(c.app_certificate == "cert");
}

TEST_CASE("no token and no certificate means none") {
  auto c = parse_config({{"AGORA_APP_ID", "app"}}, {"--channel", "room1"});
  CHECK(c.token_source == TokenSource::None);
}

TEST_CASE("uid defaults to 0 and parses when given") {
  auto def = parse_config({{"AGORA_APP_ID", "app"}}, {"--channel", "room1"});
  CHECK(def.uid == 0);
  auto set = parse_config({{"AGORA_APP_ID", "app"}},
                          {"--channel", "room1", "--uid", "1001"});
  CHECK(set.uid == 1001);
}

TEST_CASE("token-ttl defaults to 3600 and parses when given") {
  auto def = parse_config({{"AGORA_APP_ID", "app"}}, {"--channel", "room1"});
  CHECK(def.token_ttl == 3600);
  auto set = parse_config({{"AGORA_APP_ID", "app"}},
                          {"--channel", "room1", "--token-ttl", "600"});
  CHECK(set.token_ttl == 600);
}

TEST_CASE("malformed uid throws") {
  CHECK_THROWS_AS(
      parse_config({{"AGORA_APP_ID", "app"}}, {"--channel", "room1", "--uid", "abc"}),
      std::runtime_error);
}

TEST_CASE("uid overflow throws") {
  CHECK_THROWS_AS(
      parse_config({{"AGORA_APP_ID", "app"}},
                   {"--channel", "room1", "--uid", "4294967296"}),
      std::runtime_error);
}

TEST_CASE("negative uid throws") {
  CHECK_THROWS_AS(
      parse_config({{"AGORA_APP_ID", "app"}},
                   {"--channel", "room1", "--uid", "-5"}),
      std::runtime_error);
}

TEST_CASE("unknown flag throws") {
  CHECK_THROWS_AS(
      parse_config({{"AGORA_APP_ID", "app"}},
                   {"--channel", "room1", "--bogus"}),
      std::runtime_error);
}
