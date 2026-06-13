// Fake source and channel used by orchestration and CLI tests, plus
// GMock interfaces for tests that want strict expectations.
#pragma once

#include <gmock/gmock.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "rocket_watcher/channel.hpp"
#include "rocket_watcher/source.hpp"
#include "tests/test_helpers.hpp"

namespace rocket_watcher::testing {

class MockLaunchScheduleSource : public LaunchScheduleSource {
 public:
  MOCK_METHOD(FetchResult, fetch, (), (override));
};

class MockNotificationChannel : public NotificationChannel {
 public:
  MOCK_METHOD(DeliveryResult, send, (const Message& message, const std::string& recipientName),
              (override));
};

class FakeSource : public LaunchScheduleSource {
 public:
  explicit FakeSource(std::vector<Launch> launches = {}, std::vector<std::string> warnings = {},
                      std::string error = {})
      : launches_(std::move(launches)), warnings_(std::move(warnings)), error_(std::move(error)) {}

  FetchResult fetch() override {
    ++fetchCalls;
    if (!error_.empty()) throw SourceError(error_);
    return FetchResult{launches_, kNow, warnings_};
  }

  int fetchCalls = 0;

 private:
  std::vector<Launch> launches_;
  std::vector<std::string> warnings_;
  std::string error_;
};

class FakeChannel : public NotificationChannel {
 public:
  explicit FakeChannel(std::set<std::string> failFor = {}) : failFor_(std::move(failFor)) {}

  DeliveryResult send(const Message& message, const std::string& recipientName) override {
    sent.emplace_back(message, recipientName);
    if (failFor_.contains(recipientName)) {
      return DeliveryResult{recipientName, false, "boom"};
    }
    return DeliveryResult{recipientName, true, "delivered"};
  }

  std::vector<std::string> sentRecipients() const {
    std::vector<std::string> names;
    for (const auto& [message, name] : sent) names.push_back(name);
    return names;
  }

  std::vector<std::pair<Message, std::string>> sent;

 private:
  std::set<std::string> failFor_;
};

}  // namespace rocket_watcher::testing
