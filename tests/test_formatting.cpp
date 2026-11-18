#include "rocket_watcher/formatting.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rocket_watcher/time_utils.hpp"
#include "tests/test_helpers.hpp"

namespace rocket_watcher::testing {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::StartsWith;

// kNow is 2026-06-09 18:00 UTC -> 11:00 PDT.
const std::chrono::time_zone* la() { return locateZone("America/Los_Angeles"); }

Alert makeAlert(AlertKind kind, const Launch& launch, TimePoint createdAt = kNow) {
  Alert alert;
  alert.kind = kind;
  alert.launch = launch;
  alert.createdAt = createdAt;
  alert.matchedTerms = {"Starlink"};
  return alert;
}

Alert makeCorrectionAlert(const Launch& launch, CorrectionReason reason,
                          const LaunchFacts& previous) {
  Alert alert = makeAlert(AlertKind::kCorrection, launch);
  alert.reason = reason;
  alert.previous = previous;
  return alert;
}

LaunchFacts previousGoFacts() {
  return LaunchFacts{kNow + 1h, TimingPrecision::kPrecise, LaunchStatus::kGo};
}

TEST(Formatting, LaunchSoonMessageUsesDisplayTimezone) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 45min;
  Message message = formatAlert(makeAlert(AlertKind::kLaunchSoon, launch), la());
  EXPECT_EQ(message.title, "Launch soon: Starlink Group 12-34");
  EXPECT_THAT(message.body, HasSubstr("2026-06-09 11:45 PDT"));
  EXPECT_THAT(message.body, HasSubstr("~45 min"));
  EXPECT_EQ(message.alertKind, "launch_soon");
  EXPECT_EQ(message.launchKey, "ll2:abc-123");
}

TEST(Formatting, LaunchImminentMessageUrgesLiveFeed) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 5min;
  Message message = formatAlert(makeAlert(AlertKind::kLaunchImminent, launch), la());
  EXPECT_THAT(message.title, StartsWith("Launch imminent:"));
  EXPECT_THAT(message.body, HasSubstr("live feed"));
  EXPECT_THAT(message.body, HasSubstr("11:05 PDT"));
}

TEST(Formatting, HourPrecisionMessageIsSoft) {
  Launch launch = makeLaunch();
  launch.precision = TimingPrecision::kHour;
  launch.launchTime = kNow + 30min;
  Message message = formatAlert(makeAlert(AlertKind::kHourPrecision, launch), la());
  EXPECT_THAT(message.title, StartsWith("Possible launch this hour:"));
  EXPECT_THAT(message.body, HasSubstr("around 11:00 PDT"));
}

TEST(Formatting, NoGoCorrectionTitleIsReasonSpecific) {
  Launch launch = makeLaunch();
  launch.status = LaunchStatus::kNoGo;
  Message message =
      formatAlert(makeCorrectionAlert(launch, CorrectionReason::kNoGo, previousGoFacts()), la());
  EXPECT_THAT(message.title, StartsWith("Launch no longer go:"));
  EXPECT_THAT(message.body, HasSubstr("No longer go for launch"));
  EXPECT_THAT(message.body, HasSubstr("12:00 PDT"));
  EXPECT_EQ(message.alertKind, "correction:no_go");
}

TEST(Formatting, TimeChangedCorrectionShowsOldAndNewTimes) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 2h;
  Message message = formatAlert(
      makeCorrectionAlert(launch, CorrectionReason::kTimeChanged, previousGoFacts()), la());
  EXPECT_THAT(message.title, StartsWith("Launch time changed:"));
  EXPECT_THAT(message.body, HasSubstr("13:00 PDT"));  // new
  EXPECT_THAT(message.body, HasSubstr("12:00 PDT"));  // old
  EXPECT_EQ(message.alertKind, "correction:time_changed");
}

TEST(Formatting, TimingNowImpreciseCorrection) {
  Launch launch = makeLaunch();
  launch.precision = TimingPrecision::kImprecise;
  Message message = formatAlert(
      makeCorrectionAlert(launch, CorrectionReason::kTimingNowImprecise, previousGoFacts()), la());
  EXPECT_THAT(message.title, StartsWith("Launch timing now uncertain:"));
  EXPECT_THAT(message.body, HasSubstr("no longer precise"));
  EXPECT_EQ(message.alertKind, "correction:timing_now_imprecise");
}

TEST(Formatting, MatchedTermsNotShownInBody) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 45min;
  Message message = formatAlert(makeAlert(AlertKind::kLaunchSoon, launch), la());
  EXPECT_THAT(message.body, Not(HasSubstr("matched")));
  EXPECT_THAT(message.body, Not(HasSubstr("Matched")));
}

TEST(Formatting, BodyOpensWithVehicleAndShortenedPad) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 45min;
  launch.vehicle = "Falcon 9";
  launch.padName = "Space Launch Complex 4E, Vandenberg SFB, CA, USA";
  Message message = formatAlert(makeAlert(AlertKind::kLaunchSoon, launch), la());
  EXPECT_EQ(message.body,
            "Falcon 9 from SLC-4E, Vandenberg SFB, CA\n"
            "Estimated liftoff: 2026-06-09 11:45 PDT (~45 min).");
  EXPECT_EQ(message.launchName, "Falcon 9 Block 5 | Starlink Group 12-34");
}

TEST(Formatting, LaunchComplexAndNonUsPadsAreShortenedSensibly) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 45min;
  launch.vehicle = "Falcon 9";
  launch.padName = "Launch Complex 39A, Kennedy Space Center, FL, USA";
  EXPECT_THAT(formatAlert(makeAlert(AlertKind::kLaunchSoon, launch), la()).body,
              StartsWith("Falcon 9 from LC-39A, Kennedy Space Center, FL\n"));
  launch.padName = "Orbital Launch Pad 2, SpaceX Starbase, TX, USA";
  EXPECT_THAT(formatAlert(makeAlert(AlertKind::kLaunchSoon, launch), la()).body,
              StartsWith("Falcon 9 from Orbital Launch Pad 2, SpaceX Starbase, TX\n"));
  launch.padName = "ELA-4, Guiana Space Centre, French Guiana";
  EXPECT_THAT(formatAlert(makeAlert(AlertKind::kLaunchSoon, launch), la()).body,
              StartsWith("Falcon 9 from ELA-4, Guiana Space Centre, French Guiana\n"));
}

TEST(Formatting, VehicleFallsBackToLaunchNamePrefix) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 45min;
  launch.vehicle.reset();
  launch.padName.reset();
  Message message = formatAlert(makeAlert(AlertKind::kLaunchSoon, launch), la());
  EXPECT_THAT(message.body, StartsWith("Falcon 9 Block 5\nEstimated liftoff"));
}

TEST(Formatting, NameWithoutSeparatorIsUsedWholeAndPadStandsAlone) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 45min;
  launch.name = "Mystery Mission";
  launch.vehicle.reset();
  launch.padName = "SLC-40, Cape Canaveral";
  Message message = formatAlert(makeAlert(AlertKind::kLaunchSoon, launch), la());
  EXPECT_EQ(message.title, "Launch soon: Mystery Mission");
  EXPECT_THAT(message.body, StartsWith("From SLC-40, Cape Canaveral\nEstimated liftoff"));
  launch.padName.reset();
  message = formatAlert(makeAlert(AlertKind::kLaunchSoon, launch), la());
  EXPECT_THAT(message.body, StartsWith("Estimated liftoff"));
}

TEST(Formatting, CorrectionsAlsoShowVehicleAndPad) {
  Launch launch = makeLaunch();
  launch.launchTime = kNow + 2h;
  launch.vehicle = "Starship";
  launch.name = "Starship | Flight 13";
  launch.padName = "Orbital Launch Pad 1, SpaceX Starbase, TX, USA";
  Message message = formatAlert(
      makeCorrectionAlert(launch, CorrectionReason::kTimeChanged, previousGoFacts()), la());
  EXPECT_EQ(message.title, "Launch time changed: Flight 13");
  EXPECT_THAT(message.body,
              StartsWith("Starship from Orbital Launch Pad 1, SpaceX Starbase, TX\n"));
}

TEST(Formatting, WinterTimeUsesStandardTimezoneName) {
  TimePoint winter = std::chrono::sys_days{std::chrono::year{2026} / 1 / 9} + 18h;
  Launch launch = makeLaunch();
  launch.launchTime = winter + 45min;
  Message message = formatAlert(makeAlert(AlertKind::kLaunchSoon, launch, winter), la());
  EXPECT_THAT(message.body, HasSubstr("PST"));
}

}  // namespace
}  // namespace rocket_watcher::testing
