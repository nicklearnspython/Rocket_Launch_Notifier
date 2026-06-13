#include "rocket_watcher/orchestration.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <set>

#include "tests/fakes.hpp"
#include "tests/test_helpers.hpp"

namespace rocket_watcher::testing {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

class FailingSaveStore : public JsonAlertRecordStore {
 public:
  using JsonAlertRecordStore::JsonAlertRecordStore;
  void save(const std::vector<AlertRecord>&) override { throw StorageError("disk full"); }
};

Launch soonLaunch() {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 45min;
  return launch;
}

struct CycleHarness {
  CycleOutcome run(LaunchScheduleSource& source, NotificationChannel& channel,
                   AlertRecordStore* customStore = nullptr, bool dryRun = false) {
    Config config = makeConfig();
    AlertRecordStore& used_store = (customStore != nullptr) ? *customStore : store;
    CycleOptions options;
    options.nowFn = [] { return kNow; };
    options.dryRun = dryRun;
    return runCycle(source, used_store, channel, config, logger(), options);
  }

  Logger logger() {
    return [this](const std::string& line) { logs.push_back(line); };
  }

  bool logsContain(const std::string& fragment) const {
    return std::any_of(logs.begin(), logs.end(), [&](const std::string& line) {
      return line.find(fragment) != std::string::npos;
    });
  }

  TempDir dir;
  JsonAlertRecordStore store{dir / "records.json"};
  std::vector<std::string> logs;
};

TEST(Orchestration, FullCycleSavesRecordAndNotifiesAllRecipients) {
  CycleHarness harness;
  FakeSource source({soonLaunch()});
  FakeChannel channel;
  CycleOutcome outcome = harness.run(source, channel);
  EXPECT_TRUE(outcome.ok());
  EXPECT_EQ(outcome.alertsCreated, 1);
  // Record persisted before delivery, and notification attempted per recipient.
  std::vector<AlertRecord> saved = harness.store.load();
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_EQ(saved[0].key, "ll2:abc-123");
  EXPECT_THAT(channel.sentRecipients(), ElementsAre("alice", "bob"));
  std::set<std::string> titles;
  for (const auto& [message, name] : channel.sent) titles.insert(message.title);
  EXPECT_THAT(titles, ElementsAre("Launch soon: Falcon 9 Block 5 | Starlink Group 12-34"));
}

TEST(Orchestration, DryRunSendsNothingAndWritesNothing) {
  CycleHarness harness;
  FakeSource source({soonLaunch()});
  FakeChannel channel;
  CycleOutcome outcome = harness.run(source, channel, nullptr, /*dryRun=*/true);
  EXPECT_TRUE(outcome.ok());
  EXPECT_EQ(outcome.alertsCreated, 1);
  EXPECT_THAT(channel.sent, IsEmpty());
  EXPECT_FALSE(std::filesystem::exists(harness.store.path()));
  EXPECT_TRUE(harness.logsContain("dry run: would send"));
}

TEST(Orchestration, SourceFailureIsLoggedAndReported) {
  CycleHarness harness;
  FakeSource source({}, {}, "LL2 down");
  FakeChannel channel;
  CycleOutcome outcome = harness.run(source, channel);
  EXPECT_TRUE(outcome.sourceFailed);
  EXPECT_FALSE(outcome.ok());
  EXPECT_THAT(channel.sent, IsEmpty());
  EXPECT_TRUE(harness.logsContain("source fetch failed"));
}

TEST(Orchestration, SourceWarningsAreLogged) {
  CycleHarness harness;
  FakeSource source({}, {"odd precision 'Fortnight'"});
  FakeChannel channel;
  CycleOutcome outcome = harness.run(source, channel);
  EXPECT_TRUE(outcome.ok());
  EXPECT_TRUE(harness.logsContain("source warning: odd precision"));
}

TEST(Orchestration, SaveFailureBlocksAllNotifications) {
  CycleHarness harness;
  FakeSource source({soonLaunch()});
  FakeChannel channel;
  FailingSaveStore failing_store(harness.dir / "records.json");
  CycleOutcome outcome = harness.run(source, channel, &failing_store);
  EXPECT_TRUE(outcome.saveFailed);
  EXPECT_FALSE(outcome.ok());
  EXPECT_THAT(channel.sent, IsEmpty());
  EXPECT_TRUE(harness.logsContain("failed to save alert records"));
}

TEST(Orchestration, NotificationFailureDoesNotRollBackRecord) {
  CycleHarness harness;
  FakeSource source({soonLaunch()});
  FakeChannel channel({"alice"});
  CycleOutcome outcome = harness.run(source, channel);
  EXPECT_TRUE(outcome.anyDeliveryFailed());
  EXPECT_FALSE(outcome.ok());
  // Record stays saved; both recipients were still attempted.
  std::vector<AlertRecord> saved = harness.store.load();
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_EQ(saved[0].key, "ll2:abc-123");
  EXPECT_THAT(channel.sentRecipients(), ElementsAre("alice", "bob"));
  EXPECT_TRUE(harness.logsContain("DELIVERY FAILED to alice"));
  EXPECT_TRUE(harness.logsContain("delivered to bob"));
}

TEST(Orchestration, DuplicateCycleSendsNothingNew) {
  CycleHarness harness;
  FakeSource source({soonLaunch()});
  FakeChannel channel;
  harness.run(source, channel);
  CycleOutcome outcome = harness.run(source, channel);
  EXPECT_TRUE(outcome.ok());
  EXPECT_EQ(outcome.alertsCreated, 0);
  EXPECT_EQ(channel.sent.size(), 2u);  // only from the first cycle
}

TEST(Orchestration, CorruptRecordsFileBlocksNotifications) {
  CycleHarness harness;
  std::ofstream(harness.store.path()) << "{corrupt";
  FakeSource source({soonLaunch()});
  FakeChannel channel;
  CycleOutcome outcome = harness.run(source, channel);
  EXPECT_TRUE(outcome.saveFailed);
  EXPECT_THAT(channel.sent, IsEmpty());
}

// GMock-based variant proving the orchestration honors the channel contract:
// one send per recipient, formatted message content only.
TEST(Orchestration, MockChannelReceivesFormattedMessagePerRecipient) {
  CycleHarness harness;
  FakeSource source({soonLaunch()});
  MockNotificationChannel channel;
  EXPECT_CALL(channel, send(::testing::Field(&Message::title, HasSubstr("Launch soon")), "alice"))
      .WillOnce(::testing::Return(DeliveryResult{"alice", true, "delivered"}));
  EXPECT_CALL(channel, send(::testing::Field(&Message::title, HasSubstr("Launch soon")), "bob"))
      .WillOnce(::testing::Return(DeliveryResult{"bob", true, "delivered"}));
  CycleOutcome outcome = harness.run(source, channel);
  EXPECT_TRUE(outcome.ok());
}

}  // namespace
}  // namespace rocket_watcher::testing
