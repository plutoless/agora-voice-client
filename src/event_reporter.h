#pragma once
#include <cstdint>
#include <mutex>
#include <ostream>
#include <string>

namespace avc {

// Protocol version of the JSON event stream (bumped only on breaking changes).
constexpr int kEventProtocolVersion = 1;

// JSON-escape a string body (exposed for testing).
std::string json_escape(const std::string& s);

class EventReporter {
 public:
  virtual ~EventReporter() = default;
  virtual void meta(const std::string& client, const std::string& version) = 0;
  virtual void ready(const std::string& channel, std::uint32_t uid) = 0;
  virtual void peer_joined(std::uint32_t uid) = 0;
  virtual void peer_left(std::uint32_t uid) = 0;
  virtual void reconnecting() = 0;
  virtual void reconnected() = 0;
  virtual void token_renewed() = 0;
  virtual void error(int code, const std::string& message) = 0;
  virtual void fatal(int code, const std::string& message) = 0;
  virtual void stopping(const std::string& reason) = 0;
};

class JsonEventReporter : public EventReporter {
 public:
  explicit JsonEventReporter(std::ostream& os);
  void meta(const std::string& client, const std::string& version) override;
  void ready(const std::string& channel, std::uint32_t uid) override;
  void peer_joined(std::uint32_t uid) override;
  void peer_left(std::uint32_t uid) override;
  void reconnecting() override;
  void reconnected() override;
  void token_renewed() override;
  void error(int code, const std::string& message) override;
  void fatal(int code, const std::string& message) override;
  void stopping(const std::string& reason) override;
 private:
  void write_line(const std::string& body);
  std::ostream& os_;
  std::mutex mu_;
};

class HumanEventReporter : public EventReporter {
 public:
  explicit HumanEventReporter(std::ostream& os);
  void meta(const std::string& client, const std::string& version) override;
  void ready(const std::string& channel, std::uint32_t uid) override;
  void peer_joined(std::uint32_t uid) override;
  void peer_left(std::uint32_t uid) override;
  void reconnecting() override;
  void reconnected() override;
  void token_renewed() override;
  void error(int code, const std::string& message) override;
  void fatal(int code, const std::string& message) override;
  void stopping(const std::string& reason) override;
 private:
  void write_line(const std::string& body);
  std::ostream& os_;
  std::mutex mu_;
};

}  // namespace avc
