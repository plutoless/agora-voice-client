#include "doctest.h"
#include "event_reporter.h"
#include <algorithm>
#include <sstream>
#include <string>

using avc::JsonEventReporter;

namespace {
bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}
}  // namespace

TEST_CASE("json_escape handles quotes, backslashes, control chars") {
  CHECK(avc::json_escape("a\"b\\c") == "a\\\"b\\\\c");
  CHECK(avc::json_escape("line\nbreak\ttab") == "line\\nbreak\\ttab");
  CHECK(avc::json_escape(std::string("x\x01y")) == "x\\u0001y");
}

TEST_CASE("meta event carries protocol, client, version") {
  std::ostringstream os; JsonEventReporter r(os);
  r.meta("agora-voice-client", "v1.2.3");
  const std::string out = os.str();
  CHECK(contains(out, "\"type\":\"meta\""));
  CHECK(contains(out, "\"protocol\":1"));
  CHECK(contains(out, "\"client\":\"agora-voice-client\""));
  CHECK(contains(out, "\"version\":\"v1.2.3\""));
  CHECK(contains(out, "\"ts\":"));
  CHECK(out.back() == '\n');
}

TEST_CASE("ready event carries channel (escaped) and uid") {
  std::ostringstream os; JsonEventReporter r(os);
  r.ready("room\"x", 1001);
  const std::string out = os.str();
  CHECK(contains(out, "\"type\":\"ready\""));
  CHECK(contains(out, "\"channel\":\"room\\\"x\""));
  CHECK(contains(out, "\"uid\":1001"));
}

TEST_CASE("peer_joined and peer_left carry uid") {
  std::ostringstream os; JsonEventReporter r(os);
  r.peer_joined(42); r.peer_left(42);
  const std::string out = os.str();
  CHECK(contains(out, "\"type\":\"peer_joined\""));
  CHECK(contains(out, "\"type\":\"peer_left\""));
  CHECK(contains(out, "\"uid\":42"));
}

TEST_CASE("error and fatal carry code and message") {
  std::ostringstream os; JsonEventReporter r(os);
  r.error(7, "warn"); r.fatal(5, "boom");
  const std::string out = os.str();
  CHECK(contains(out, "\"type\":\"error\""));
  CHECK(contains(out, "\"code\":7"));
  CHECK(contains(out, "\"message\":\"warn\""));
  CHECK(contains(out, "\"type\":\"fatal\""));
  CHECK(contains(out, "\"code\":5"));
  CHECK(contains(out, "\"message\":\"boom\""));
}

TEST_CASE("stopping carries reason; bare events well-formed; line count") {
  std::ostringstream os; JsonEventReporter r(os);
  r.reconnecting(); r.reconnected(); r.token_renewed(); r.stopping("signal");
  const std::string out = os.str();
  CHECK(contains(out, "\"type\":\"reconnecting\""));
  CHECK(contains(out, "\"type\":\"reconnected\""));
  CHECK(contains(out, "\"type\":\"token_renewed\""));
  CHECK(contains(out, "\"type\":\"stopping\""));
  CHECK(contains(out, "\"reason\":\"signal\""));
  CHECK(std::count(out.begin(), out.end(), '\n') == 4);
}
