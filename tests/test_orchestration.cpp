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

  // Runs one watch-style cycle at `now`, sharing log memory across calls.
  CycleOutcome watchCycle(LaunchScheduleSource& source, NotificationChannel& channel,
                          TimePoint now = kNow, bool verbose = false) {
    CycleOptions options;
    options.nowFn = [now] { return now; };
    options.verbose = verbose;
    options.memory = &memory;
    return runCycle(source, store, channel, makeConfig(), logger(), options);
  }

  std::vector<std::string> logsWith(const std::string& fragment) const {
    std::vector<std::string> matches;
    for (const auto& line : logs) {
      if (line.find(fragment) != std::string::npos) matches.push_back(line);
    }
    return matches;
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
  LogMemory memory;
};

Launch farLaunch() {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 40h;  // beyond the 30h lookahead
  return launch;
}

Launch otherProviderLaunch() {
  Launch launch = makeLaunch();
  launch.sourceLaunchId = "rl-1";
  launch.name = "Electron | Some Payload";
  launch.provider = "Rocket Lab";
  return launch;
}

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
  EXPECT_THAT(titles, ElementsAre("Launch soon: Starlink Group 12-34"));
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

TEST(QuietLogging, NoPerLaunchEvaluationLinesByDefault) {
  CycleHarness harness;
  FakeSource source({farLaunch(), otherProviderLaunch()});
  FakeChannel channel;
  harness.watchCycle(source, channel);
  EXPECT_THAT(harness.logsWith("eval:"), IsEmpty());
  EXPECT_THAT(harness.logsWith("fetched"), IsEmpty());
  EXPECT_THAT(harness.logsWith("Rocket Lab"), IsEmpty());
}

TEST(QuietLogging, VerboseLogsEveryEvaluation) {
  CycleHarness harness;
  FakeSource source({farLaunch(), otherProviderLaunch()});
  FakeChannel channel;
  harness.watchCycle(source, channel, kNow, /*verbose=*/true);
  EXPECT_EQ(harness.logsWith("eval:").size(), 2u);
  EXPECT_TRUE(harness.logsContain("fetched 2 upcoming launches"));
  EXPECT_THAT(harness.logsWith("now tracking"), IsEmpty());
}

TEST(QuietLogging, DryRunLogsEveryEvaluation) {
  CycleHarness harness;
  FakeSource source({farLaunch(), otherProviderLaunch()});
  FakeChannel channel;
  harness.run(source, channel, nullptr, /*dryRun=*/true);
  EXPECT_EQ(harness.logsWith("eval:").size(), 2u);
}

TEST(QuietLogging, TrackedLaunchLoggedOnceThenOnlyOnChange) {
  CycleHarness harness;
  FakeChannel channel;
  FakeSource far_source({farLaunch(), otherProviderLaunch()});
  harness.watchCycle(far_source, channel);
  harness.watchCycle(far_source, channel, kNow + 5min);
  EXPECT_THAT(harness.logsWith("now tracking"),
              ElementsAre(HasSubstr("Starlink Group 12-34: silent (beyond schedule lookahead)")));

  Launch unknown = farLaunch();
  unknown.status = LaunchStatus::kUnknown;
  FakeSource unknown_source({unknown});
  harness.watchCycle(unknown_source, channel, kNow + 10min);
  EXPECT_THAT(
      harness.logsWith("tracking update"),
      ElementsAre(HasSubstr("silent (beyond schedule lookahead) -> silent (status unknown)")));
}

TEST(QuietLogging, LaunchLeavingTheScheduleIsLogged) {
  CycleHarness harness;
  FakeChannel channel;
  FakeSource with_launch({farLaunch()});
  FakeSource empty_source({});
  harness.watchCycle(with_launch, channel);
  harness.watchCycle(empty_source, channel, kNow + 5min);
  EXPECT_THAT(harness.logsWith("no longer tracking"),
              ElementsAre(HasSubstr("Starlink Group 12-34 (left the upcoming schedule)")));
}

TEST(QuietLogging, AlertCycleIsNotAlsoReportedAsTrackingChange) {
  CycleHarness harness;
  FakeChannel channel;
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 70min;  // tracked, not yet in the soon window
  FakeSource source({launch});
  harness.watchCycle(source, channel);
  harness.watchCycle(source, channel, kNow + 15min);  // Launch Soon fires
  harness.watchCycle(source, channel, kNow + 20min);  // back to waiting
  EXPECT_EQ(harness.logsWith("alert created").size(), 1u);
  EXPECT_EQ(harness.logsWith("now tracking").size(), 1u);
  EXPECT_THAT(harness.logsWith("tracking update"), IsEmpty());
}

TEST(QuietLogging, StatusLineLoggedHourly) {
  CycleHarness harness;
  FakeChannel channel;
  FakeSource source({farLaunch(), otherProviderLaunch()});
  FakeSource failing({}, {}, "LL2 down");
  harness.watchCycle(source, channel);
  EXPECT_THAT(harness.logsWith("status:"),
              ElementsAre(HasSubstr("2 upcoming launches evaluated, 1 tracked; 1 check(s) since "
                                    "last status, 0 failed")));
  for (int i = 1; i < 12; ++i) {
    harness.watchCycle(i == 3 ? static_cast<LaunchScheduleSource&>(failing) : source, channel,
                       kNow + i * 5min);
  }
  EXPECT_EQ(harness.logsWith("status:").size(), 1u);
  harness.watchCycle(source, channel, kNow + 60min);
  auto status = harness.logsWith("status:");
  ASSERT_EQ(status.size(), 2u);
  EXPECT_THAT(status[1], HasSubstr("12 check(s) since last status, 1 failed"));
}

TEST(QuietLogging, RepeatedSourceWarningLoggedOnce) {
  CycleHarness harness;
  FakeChannel channel;
  FakeSource source({}, {"odd precision 'Fortnight'"});
  harness.watchCycle(source, channel);
  harness.watchCycle(source, channel, kNow + 5min);
  EXPECT_EQ(harness.logsWith("source warning").size(), 1u);
}

TEST(QuietLogging, FailuresAreAlwaysLogged) {
  CycleHarness harness;
  FakeChannel channel;
  FakeSource failing({}, {}, "LL2 down");
  harness.watchCycle(failing, channel);
  harness.watchCycle(failing, channel, kNow + 5min);
  EXPECT_EQ(harness.logsWith("source fetch failed").size(), 2u);
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
