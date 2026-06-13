#include "rocket_watcher/core.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tests/test_helpers.hpp"

namespace rocket_watcher::testing {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

const std::string kProvider = "SpaceX";
const std::vector<std::string> kTerms = {"Starlink"};

CycleDecision decide(const std::vector<Launch>& launches,
                     const std::vector<AlertRecord>& records = {}, TimePoint now = kNow) {
  return decideCycle(launches, records, makePolicy(), kProvider, kTerms, now);
}

std::vector<AlertKind> alertKinds(const CycleDecision& decision) {
  std::vector<AlertKind> kinds;
  for (const auto& alert : decision.alerts) kinds.push_back(alert.kind);
  return kinds;
}

LaunchFacts goPreciseFacts(const Launch& launch) {
  return LaunchFacts{launch.launchTime, TimingPrecision::kPrecise, LaunchStatus::kGo};
}

TEST(CountdownAlerts, LaunchSoonAlertInsideSoonWindow) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 45min;
  CycleDecision decision = decide({launch});
  EXPECT_THAT(alertKinds(decision), ElementsAre(AlertKind::kLaunchSoon));
  EXPECT_EQ(decision.alerts[0].launch, launch);
  EXPECT_THAT(decision.alerts[0].matchedTerms, ElementsAre("Starlink"));
}

TEST(CountdownAlerts, LaunchImminentAlertInsideImminentWindow) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 5min;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon});
  CycleDecision decision = decide({launch}, {record});
  EXPECT_THAT(alertKinds(decision), ElementsAre(AlertKind::kLaunchImminent));
}

TEST(CountdownAlerts, MostUrgentUnsentAlertWinsWhenBothEligible) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 5min;
  CycleDecision decision = decide({launch});
  EXPECT_THAT(alertKinds(decision), ElementsAre(AlertKind::kLaunchImminent));
}

TEST(CountdownAlerts, NoAlertOutsideSoonWindow) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 4h;
  EXPECT_THAT(decide({launch}).alerts, IsEmpty());
}

TEST(CountdownAlerts, BeyondScheduleLookaheadIsSilent) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 31h;
  CycleDecision decision = decide({launch});
  EXPECT_THAT(decision.alerts, IsEmpty());
  EXPECT_THAT(decision.evaluations, Contains(HasSubstr("lookahead")));
}

TEST(CountdownAlerts, PassedLaunchTimeIsSilent) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow - 5min;
  EXPECT_THAT(decide({launch}).alerts, IsEmpty());
}

TEST(DuplicateSuppression, SoonNotRepeatedWhileStillInWindow) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 40min;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon});
  EXPECT_THAT(decide({launch}, {record}).alerts, IsEmpty());
}

TEST(DuplicateSuppression, NoSoonAfterImminentAlreadySent) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 40min;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchImminent});
  EXPECT_THAT(decide({launch}, {record}).alerts, IsEmpty());
}

TEST(DuplicateSuppression, ImminentNotRepeated) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 5min;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon, AlertKind::kLaunchImminent});
  EXPECT_THAT(decide({launch}, {record}).alerts, IsEmpty());
}

TEST(HourPrecision, HourPrecisionAlertWhenHourWindowNear) {
  Launch launch = makeLaunch();
  launch.precision = TimingPrecision::kHour;
  launch.launchTime = kNow + 30min;
  CycleDecision decision = decide({launch});
  EXPECT_THAT(alertKinds(decision), ElementsAre(AlertKind::kHourPrecision));
}

TEST(HourPrecision, HourPrecisionSilentWhenHourFarAway) {
  Launch launch = makeLaunch();
  launch.precision = TimingPrecision::kHour;
  launch.launchTime = kNow + 3h;
  EXPECT_THAT(decide({launch}).alerts, IsEmpty());
}

TEST(HourPrecision, HourPrecisionNotRepeated) {
  Launch launch = makeLaunch();
  launch.precision = TimingPrecision::kHour;
  launch.launchTime = kNow + 30min;
  AlertRecord record = makeRecord(launch, {AlertKind::kHourPrecision});
  EXPECT_THAT(decide({launch}, {record}).alerts, IsEmpty());
}

TEST(HourPrecision, HourAlertDoesNotSuppressLaterPreciseAlerts) {
  Launch launch = makeLaunch();
  launch.precision = TimingPrecision::kPrecise;
  launch.launchTime = kNow + 45min;
  AlertRecord record = makeRecord(launch, {AlertKind::kHourPrecision});
  record.lastFacts = LaunchFacts{launch.launchTime, TimingPrecision::kHour, LaunchStatus::kGo};
  CycleDecision decision = decide({launch}, {record});
  EXPECT_THAT(alertKinds(decision), ElementsAre(AlertKind::kLaunchSoon));
}

TEST(SilentTimingAndStatus, ImpreciseTimingIsSilent) {
  Launch launch = makeLaunch();
  launch.precision = TimingPrecision::kImprecise;
  EXPECT_THAT(decide({launch}).alerts, IsEmpty());
}

TEST(SilentTimingAndStatus, UnknownTimingIsSilent) {
  Launch launch = makeLaunch();
  launch.precision = TimingPrecision::kUnknown;
  launch.launchTime = std::nullopt;
  EXPECT_THAT(decide({launch}).alerts, IsEmpty());
}

TEST(SilentTimingAndStatus, EndedStatusIsSilentEvenAfterPriorAlert) {
  Launch launch = makeLaunch();
  launch.status = LaunchStatus::kEnded;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon});
  EXPECT_THAT(decide({launch}, {record}).alerts, IsEmpty());
}

TEST(SilentTimingAndStatus, UnknownStatusIsSilentEvenAfterPriorAlert) {
  Launch launch = makeLaunch();
  launch.status = LaunchStatus::kUnknown;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon});
  EXPECT_THAT(decide({launch}, {record}).alerts, IsEmpty());
}

TEST(NoGoCorrections, NoGoAfterPriorAlertCreatesCorrection) {
  Launch launch = makeLaunch();
  launch.status = LaunchStatus::kNoGo;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon});
  record.lastFacts = goPreciseFacts(launch);
  CycleDecision decision = decide({launch}, {record});
  ASSERT_EQ(decision.alerts.size(), 1u);
  const Alert& alert = decision.alerts[0];
  EXPECT_EQ(alert.kind, AlertKind::kCorrection);
  EXPECT_EQ(alert.reason, CorrectionReason::kNoGo);
  ASSERT_TRUE(alert.previous.has_value());
  EXPECT_EQ(alert.previous->status, LaunchStatus::kGo);
}

TEST(NoGoCorrections, NoGoWithoutPriorAlertIsSilent) {
  Launch launch = makeLaunch();
  launch.status = LaunchStatus::kNoGo;
  EXPECT_THAT(decide({launch}).alerts, IsEmpty());
}

TEST(NoGoCorrections, NoGoCorrectionNotRepeated) {
  Launch launch = makeLaunch();
  launch.status = LaunchStatus::kNoGo;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon});
  record.lastFacts = LaunchFacts{launch.launchTime, TimingPrecision::kPrecise, LaunchStatus::kNoGo};
  EXPECT_THAT(decide({launch}, {record}).alerts, IsEmpty());
}

TEST(TimeChangedCorrections, TimeShiftAtThresholdCreatesCorrection) {
  TimePoint old_time = kNow + 2h;
  Launch launch = makeLaunch();
  launch.launchTime = old_time + 30min;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon});
  record.lastFacts = LaunchFacts{old_time, TimingPrecision::kPrecise, LaunchStatus::kGo};
  CycleDecision decision = decide({launch}, {record});
  ASSERT_EQ(decision.alerts.size(), 1u);
  const Alert& alert = decision.alerts[0];
  EXPECT_EQ(alert.kind, AlertKind::kCorrection);
  EXPECT_EQ(alert.reason, CorrectionReason::kTimeChanged);
  ASSERT_TRUE(alert.previous.has_value());
  EXPECT_EQ(alert.previous->launchTime, old_time);
}

TEST(TimeChangedCorrections, SmallWobbleBelowThresholdIsNotCorrected) {
  TimePoint old_time = kNow + 2h;
  Launch launch = makeLaunch();
  launch.launchTime = old_time + 29min;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon});
  record.lastFacts = LaunchFacts{old_time, TimingPrecision::kPrecise, LaunchStatus::kGo};
  EXPECT_THAT(decide({launch}, {record}).alerts, IsEmpty());
}

TEST(TimeChangedCorrections, TimeCorrectionWithoutPriorAlertRecordIsSilent) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 5h;
  EXPECT_THAT(decide({launch}).alerts, IsEmpty());
}

TEST(TimingNowImpreciseCorrections, PreciseToImpreciseAfterAlertCreatesCorrection) {
  Launch launch = makeLaunch();
  launch.precision = TimingPrecision::kImprecise;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon});
  record.lastFacts = goPreciseFacts(launch);
  CycleDecision decision = decide({launch}, {record});
  ASSERT_EQ(decision.alerts.size(), 1u);
  const Alert& alert = decision.alerts[0];
  EXPECT_EQ(alert.reason, CorrectionReason::kTimingNowImprecise);
  ASSERT_TRUE(alert.previous.has_value());
  EXPECT_EQ(alert.previous->precision, TimingPrecision::kPrecise);
}

TEST(TimingNowImpreciseCorrections, ImpreciseCorrectionNotRepeated) {
  Launch launch = makeLaunch();
  launch.precision = TimingPrecision::kImprecise;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon});
  record.lastFacts = LaunchFacts{launch.launchTime, TimingPrecision::kImprecise, LaunchStatus::kGo};
  EXPECT_THAT(decide({launch}, {record}).alerts, IsEmpty());
}

TEST(RelevanceGate, IrrelevantLaunchNeverAlertsAndIsExplained) {
  Launch launch = makeLaunch();
  launch.provider = "Rocket Lab";
  CycleDecision decision = decide({launch});
  EXPECT_THAT(decision.alerts, IsEmpty());
  EXPECT_THAT(decision.evaluations, Contains(HasSubstr("not relevant")));
}

TEST(RelevanceGate, RelevantLaunchEvaluationIsLoggedBeforeEligibility) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 20h;
  CycleDecision decision = decide({launch});
  EXPECT_THAT(decision.alerts, IsEmpty());
  EXPECT_THAT(decision.evaluations, Contains(HasSubstr("no unsent countdown alert eligible")));
}

TEST(AlertRecordUpdates, NewAlertCreatesRecordWithFactsAndSentAlert) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 45min;
  CycleDecision decision = decide({launch});
  ASSERT_EQ(decision.updatedRecords.size(), 1u);
  const AlertRecord& record = decision.updatedRecords[0];
  EXPECT_EQ(record.key, launch.key());
  EXPECT_EQ(record.key, "ll2:abc-123");
  EXPECT_EQ(record.sourceName, "ll2");
  EXPECT_EQ(record.sourceLaunchId, "abc-123");
  EXPECT_THAT(record.matchedTerms, ElementsAre("Starlink"));
  EXPECT_EQ(record.lastFacts.launchTime, launch.launchTime);
  ASSERT_EQ(record.sentAlerts.size(), 1u);
  EXPECT_EQ(record.sentAlerts[0].kind, AlertKind::kLaunchSoon);
  EXPECT_EQ(record.firstAlertAt, kNow);
  EXPECT_EQ(record.lastAlertAt, kNow);
}

TEST(AlertRecordUpdates, CorrectionAppendsToExistingRecordAndRefreshesFacts) {
  TimePoint old_time = kNow + 2h;
  Launch launch = makeLaunch();
  launch.launchTime = old_time + 1h;
  AlertRecord record = makeRecord(launch, {AlertKind::kLaunchSoon}, kNow - 3h);
  record.lastFacts = LaunchFacts{old_time, TimingPrecision::kPrecise, LaunchStatus::kGo};
  CycleDecision decision = decide({launch}, {record});
  ASSERT_EQ(decision.updatedRecords.size(), 1u);
  const AlertRecord& updated = decision.updatedRecords[0];
  ASSERT_EQ(updated.sentAlerts.size(), 2u);
  EXPECT_EQ(updated.sentAlerts[0].kind, AlertKind::kLaunchSoon);
  EXPECT_EQ(updated.sentAlerts[1].kind, AlertKind::kCorrection);
  EXPECT_EQ(updated.sentAlerts[1].reason, CorrectionReason::kTimeChanged);
  EXPECT_EQ(updated.lastFacts.launchTime, launch.launchTime);
  EXPECT_EQ(updated.firstAlertAt, kNow - 3h);
  EXPECT_EQ(updated.lastAlertAt, kNow);
}

TEST(AlertRecordUpdates, NoRecordCreatedWithoutAnAlert) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 20h;
  EXPECT_THAT(decide({launch}).updatedRecords, IsEmpty());
}

TEST(AlertRecordUpdates, RetentionPrunesOldRecordsAndKeepsRecent) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 20h;
  Launch old_launch = makeLaunch();
  old_launch.sourceLaunchId = "old-1";
  Launch new_launch = makeLaunch();
  new_launch.sourceLaunchId = "new-1";
  AlertRecord stale =
      makeRecord(old_launch, {AlertKind::kLaunchSoon}, kNow - std::chrono::days{31});
  AlertRecord fresh = makeRecord(new_launch, {AlertKind::kLaunchSoon}, kNow - std::chrono::days{5});
  CycleDecision decision = decide({launch}, {stale, fresh});
  ASSERT_EQ(decision.updatedRecords.size(), 1u);
  EXPECT_EQ(decision.updatedRecords[0].key, "ll2:new-1");
}

}  // namespace
}  // namespace rocket_watcher::testing
