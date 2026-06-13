#include "rocket_watcher/cli.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <memory>
#include <sstream>

#include "tests/fakes.hpp"
#include "tests/test_helpers.hpp"

namespace rocket_watcher::testing {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

const std::string kConfigBody = R"(
[relevance]
launch_provider = "SpaceX"
include_terms = ["Starlink"]

[pushover]
token_env = "PUSHOVER_TOKEN"

[[recipients]]
name = "alice"
user_key_env = "PUSHOVER_USER_ALICE"

[[recipients]]
name = "bob"
user_key_env = "PUSHOVER_USER_BOB"
)";

// A CLI harness with a temp project directory, fake secrets in an injected
// environment, and capture of stdout/stderr.
class CliTest : public ::testing::Test {
 protected:
  void SetUp() override {
    recordsPath_ = (dir_ / "records.json").generic_string();
    logPath_ = (dir_ / "watcher.log").generic_string();
    configPath_ = (dir_ / "config.toml").generic_string();
    std::ofstream(configPath_) << kConfigBody << "\n[watcher]\nalert_records_path = '"
                               << recordsPath_ << "'\nlog_path = '" << logPath_ << "'\n";
    env_ = {
        {"PUSHOVER_TOKEN", "tok"},
        {"PUSHOVER_USER_ALICE", "key-a"},
        {"PUSHOVER_USER_BOB", "key-b"},
    };
    deps_.getEnv = [this](const std::string& name) -> std::optional<std::string> {
      auto found = env_.find(name);
      if (found == env_.end()) return std::nullopt;
      return found->second;
    };
    deps_.out = &out_;
    deps_.err = &err_;
  }

  int runCli(const std::vector<std::string>& commandArgs) {
    std::vector<std::string> args = {"--config", configPath_, "--env-file", "missing.env"};
    args.insert(args.end(), commandArgs.begin(), commandArgs.end());
    return runMain(args, deps_);
  }

  void useChannel(std::shared_ptr<FakeChannel> channel) {
    deps_.buildChannel = [channel](const Config&, const PushoverSecrets&) {
      return std::make_unique<ChannelProxy>(channel);
    };
  }

  void useSource(std::vector<Launch> launches = {}, std::string error = {}) {
    deps_.buildSource = [launches = std::move(launches), error = std::move(error)](
                            const Config&) -> std::unique_ptr<LaunchScheduleSource> {
      return std::make_unique<FakeSource>(launches, std::vector<std::string>{}, error);
    };
  }

  // Lets tests keep a handle on the channel the CLI builds and uses.
  class ChannelProxy : public NotificationChannel {
   public:
    explicit ChannelProxy(std::shared_ptr<FakeChannel> target) : target_(std::move(target)) {}
    DeliveryResult send(const Message& message, const std::string& recipientName) override {
      return target_->send(message, recipientName);
    }

   private:
    std::shared_ptr<FakeChannel> target_;
  };

  TempDir dir_;
  std::string configPath_;
  std::string recordsPath_;
  std::string logPath_;
  std::map<std::string, std::string> env_;
  std::ostringstream out_;
  std::ostringstream err_;
  CliDeps deps_;
};

TEST_F(CliTest, TestNotificationBypassesSourceAndRecords) {
  auto channel = std::make_shared<FakeChannel>();
  useChannel(channel);
  deps_.buildSource = [](const Config&) -> std::unique_ptr<LaunchScheduleSource> {
    ADD_FAILURE() << "test-notification must not build a launch source";
    return nullptr;
  };
  EXPECT_EQ(runCli({"test-notification"}), 0);
  EXPECT_THAT(channel->sentRecipients(), ElementsAre("alice", "bob"));
  ASSERT_FALSE(channel->sent.empty());
  EXPECT_EQ(channel->sent[0].first.alertKind, "test");
  EXPECT_FALSE(std::filesystem::exists(recordsPath_));
}

TEST_F(CliTest, TestNotificationExitsNonzeroOnAnyFailure) {
  auto channel = std::make_shared<FakeChannel>(std::set<std::string>{"bob"});
  useChannel(channel);
  EXPECT_EQ(runCli({"test-notification"}), 1);
  // Both recipients were still attempted.
  EXPECT_THAT(channel->sentRecipients(), ElementsAre("alice", "bob"));
}

TEST_F(CliTest, MissingSecretsExitCode2) {
  env_.erase("PUSHOVER_TOKEN");
  EXPECT_EQ(runCli({"test-notification"}), 2);
  EXPECT_THAT(err_.str(), HasSubstr("configuration error"));
}

TEST_F(CliTest, InvalidConfigExitCode2) {
  std::ofstream(configPath_, std::ios::trunc)
      << "[relevance]\nlaunch_provider = 'SpaceX'\ninclude_terms = []\n";
  EXPECT_EQ(runCli({"once", "--dry-run"}), 2);
}

TEST_F(CliTest, OnceSourceFailureExitsNonzero) {
  useSource({}, "LL2 down");
  useChannel(std::make_shared<FakeChannel>());
  EXPECT_EQ(runCli({"once"}), 1);
}

TEST_F(CliTest, DryRunOnceSourceFailureExitsNonzero) {
  useSource({}, "LL2 down");
  EXPECT_EQ(runCli({"once", "--dry-run"}), 1);
}

TEST_F(CliTest, DryRunOnceSucceedsWithoutSecrets) {
  env_.erase("PUSHOVER_TOKEN");
  useSource({});
  EXPECT_EQ(runCli({"once", "--dry-run"}), 0);
  EXPECT_FALSE(std::filesystem::exists(logPath_));
  EXPECT_THAT(out_.str(), HasSubstr("dry run"));
}

TEST_F(CliTest, OnceDeliveryFailureExitsNonzero) {
  // The CLI uses the real clock, so the launch must sit in the soon
  // window relative to real time.
  Launch launch = makeLaunch();
  launch.launchTime = utcNow() + 45min;
  useSource({launch});
  useChannel(std::make_shared<FakeChannel>(std::set<std::string>{"alice"}));
  EXPECT_EQ(runCli({"once"}), 1);
}

TEST_F(CliTest, UnknownCommandExitsWithUsageError) {
  EXPECT_EQ(runCli({"frobnicate"}), 2);
  EXPECT_THAT(err_.str(), HasSubstr("unknown command"));
}

}  // namespace
}  // namespace rocket_watcher::testing
