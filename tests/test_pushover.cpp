#include "rocket_watcher/pushover.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <vector>

#include <nlohmann/json.hpp>

namespace rocket_watcher::testing {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;

const Message kMessage{
    "Launch soon: Starlink",
    "Estimated liftoff: 2026-06-09 11:45 PDT.",
    "launch_soon",
    "ll2:abc-123",
    "Starlink",
};

struct RecordedCall {
  std::string url;
  std::map<std::string, std::string> fields;
  std::chrono::seconds timeout;
};

struct ChannelFixture {
  explicit ChannelFixture(long status = 200, std::string body = R"({"status": 1})",
                          std::string error = {}) {
    PushoverChannel::Options options;
    options.token = "app-token";
    options.userKeys = {{"alice", "user-key-a"}};
    options.httpPost = [this, status, body, error](
                           const std::string& url,
                           const std::vector<std::pair<std::string, std::string>>& fields,
                           std::chrono::seconds timeout) -> HttpResponse {
      RecordedCall call{url, {fields.begin(), fields.end()}, timeout};
      calls.push_back(call);
      if (!error.empty()) throw std::runtime_error(error);
      return HttpResponse{status, body};
    };
    channel = std::make_unique<PushoverChannel>(std::move(options));
  }

  std::unique_ptr<PushoverChannel> channel;
  std::vector<RecordedCall> calls;
};

TEST(Pushover, RequestConstruction) {
  ChannelFixture fixture;
  DeliveryResult result = fixture.channel->send(kMessage, "alice");
  EXPECT_TRUE(result.ok);
  ASSERT_EQ(fixture.calls.size(), 1u);
  const RecordedCall& call = fixture.calls[0];
  EXPECT_EQ(call.url, kPushoverApiUrl);
  std::map<std::string, std::string> expected = {
      {"token", "app-token"},
      {"user", "user-key-a"},
      {"title", "Launch soon: Starlink"},
      {"message", "Estimated liftoff: 2026-06-09 11:45 PDT."},
  };
  EXPECT_EQ(call.fields, expected);
}

TEST(Pushover, ApiErrorResponseIsAFailedDelivery) {
  ChannelFixture fixture(400, R"({"status": 0, "errors": ["user key is invalid"]})");
  DeliveryResult result = fixture.channel->send(kMessage, "alice");
  EXPECT_FALSE(result.ok);
  EXPECT_THAT(result.detail, HasSubstr("user key is invalid"));
}

TEST(Pushover, NetworkExceptionIsAFailedDelivery) {
  ChannelFixture fixture(200, "", "timeout");
  DeliveryResult result = fixture.channel->send(kMessage, "alice");
  EXPECT_FALSE(result.ok);
  EXPECT_THAT(result.detail, HasSubstr("timeout"));
}

TEST(Pushover, UnknownRecipientFailsWithoutRequest) {
  ChannelFixture fixture;
  DeliveryResult result = fixture.channel->send(kMessage, "mallory");
  EXPECT_FALSE(result.ok);
  EXPECT_THAT(fixture.calls, IsEmpty());
}

TEST(Pushover, NonJsonResponseIsAFailedDelivery) {
  ChannelFixture fixture(500, "<html>oops</html>");
  DeliveryResult result = fixture.channel->send(kMessage, "alice");
  EXPECT_FALSE(result.ok);
  EXPECT_THAT(result.detail, HasSubstr("HTTP 500"));
}

}  // namespace
}  // namespace rocket_watcher::testing
