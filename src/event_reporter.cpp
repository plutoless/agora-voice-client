#include "event_reporter.h"
#include <chrono>
#include <cstdio>
#include <sstream>

namespace avc {
namespace {
long long now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
}  // namespace

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[7];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

JsonEventReporter::JsonEventReporter(std::ostream& os) : os_(os) {}
void JsonEventReporter::write_line(const std::string& body) {
  std::lock_guard<std::mutex> lock(mu_);
  os_ << body << "\n";
  os_.flush();
}
void JsonEventReporter::meta(const std::string& client, const std::string& version) {
  std::ostringstream b;
  b << "{\"type\":\"meta\",\"ts\":" << now_ms()
    << ",\"protocol\":" << kEventProtocolVersion
    << ",\"client\":\"" << json_escape(client) << "\""
    << ",\"version\":\"" << json_escape(version) << "\"}";
  write_line(b.str());
}
void JsonEventReporter::ready(const std::string& channel, std::uint32_t uid) {
  std::ostringstream b;
  b << "{\"type\":\"ready\",\"ts\":" << now_ms()
    << ",\"channel\":\"" << json_escape(channel) << "\",\"uid\":" << uid << "}";
  write_line(b.str());
}
void JsonEventReporter::peer_joined(std::uint32_t uid) {
  std::ostringstream b;
  b << "{\"type\":\"peer_joined\",\"ts\":" << now_ms() << ",\"uid\":" << uid << "}";
  write_line(b.str());
}
void JsonEventReporter::peer_left(std::uint32_t uid) {
  std::ostringstream b;
  b << "{\"type\":\"peer_left\",\"ts\":" << now_ms() << ",\"uid\":" << uid << "}";
  write_line(b.str());
}
void JsonEventReporter::reconnecting() {
  std::ostringstream b; b << "{\"type\":\"reconnecting\",\"ts\":" << now_ms() << "}";
  write_line(b.str());
}
void JsonEventReporter::reconnected() {
  std::ostringstream b; b << "{\"type\":\"reconnected\",\"ts\":" << now_ms() << "}";
  write_line(b.str());
}
void JsonEventReporter::token_renewed() {
  std::ostringstream b; b << "{\"type\":\"token_renewed\",\"ts\":" << now_ms() << "}";
  write_line(b.str());
}
void JsonEventReporter::error(int code, const std::string& message) {
  std::ostringstream b;
  b << "{\"type\":\"error\",\"ts\":" << now_ms()
    << ",\"code\":" << code << ",\"message\":\"" << json_escape(message) << "\"}";
  write_line(b.str());
}
void JsonEventReporter::fatal(int code, const std::string& message) {
  std::ostringstream b;
  b << "{\"type\":\"fatal\",\"ts\":" << now_ms()
    << ",\"code\":" << code << ",\"message\":\"" << json_escape(message) << "\"}";
  write_line(b.str());
}
void JsonEventReporter::stopping(const std::string& reason) {
  std::ostringstream b;
  b << "{\"type\":\"stopping\",\"ts\":" << now_ms()
    << ",\"reason\":\"" << json_escape(reason) << "\"}";
  write_line(b.str());
}

HumanEventReporter::HumanEventReporter(std::ostream& os) : os_(os) {}
void HumanEventReporter::write_line(const std::string& body) {
  std::lock_guard<std::mutex> lock(mu_);
  os_ << "[avc] " << body << "\n";
  os_.flush();
}
void HumanEventReporter::meta(const std::string& client, const std::string& version) {
  write_line(client + " " + version);
}
void HumanEventReporter::ready(const std::string& channel, std::uint32_t uid) {
  std::ostringstream b; b << "ready channel=" << channel << " uid=" << uid; write_line(b.str());
}
void HumanEventReporter::peer_joined(std::uint32_t uid) {
  std::ostringstream b; b << "peer joined uid=" << uid; write_line(b.str());
}
void HumanEventReporter::peer_left(std::uint32_t uid) {
  std::ostringstream b; b << "peer left uid=" << uid; write_line(b.str());
}
void HumanEventReporter::reconnecting() { write_line("reconnecting"); }
void HumanEventReporter::reconnected() { write_line("reconnected"); }
void HumanEventReporter::token_renewed() { write_line("token renewed"); }
void HumanEventReporter::error(int code, const std::string& message) {
  std::ostringstream b; b << "error code=" << code << " " << message; write_line(b.str());
}
void HumanEventReporter::fatal(int code, const std::string& message) {
  std::ostringstream b; b << "fatal code=" << code << " " << message; write_line(b.str());
}
void HumanEventReporter::stopping(const std::string& reason) {
  write_line("stopping reason=" + reason);
}

}  // namespace avc
