#include "rocket_watcher/digest.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>

#include "rocket_watcher/storage.hpp"
#include "rocket_watcher/time_utils.hpp"
#include "tests/test_helpers.hpp"

namespace rocket_watcher::testing {
namespace {

using ::testing::ElementsAre;

const std::chrono::time_zone* pacific() { return locateZone("America/Los_Angeles"); }

TimePoint utc(int year, unsigned month, unsigned day, int hour, int minute = 0) {
  return std::chrono::sys_days{std::chrono::year{year} / month / day} + std::chrono::hours{hour} +
         std::chrono::minutes{minute};
}

// Sunday 2026-06-07 20:00 PDT, the slot before kNow (Tuesday 11:00 PDT).
const TimePoint kSundaySlot = utc(2026, 6, 8, 3);

TEST(DigestSchedule, LatestSlotIsPreviousSundayEvening) {
  EXPECT_EQ(latestDigestSlot(kNow, DigestConfig{}, pacific()), kSundaySlot);
}

TEST(DigestSchedule, SlotStartsExactlyAtScheduledTime) {
  EXPECT_EQ(latestDigestSlot(kSundaySlot, DigestConfig{}, pacific()), kSundaySlot);
  EXPECT_EQ(latestDigestSlot(kSundaySlot - 1s, DigestConfig{}, pacific()),
            kSundaySlot - std::chrono::days{7});
}

TEST(DigestSchedule, HonorsConfiguredDayAndTime) {
  DigestConfig digest;
  digest.day = std::chrono::Friday;
  digest.timeOfDay = 7h + 30min;
  // Friday 2026-06-05 07:30 PDT.
  EXPECT_EQ(latestDigestSlot(kNow, digest, pacific()), utc(2026, 6, 5, 14, 30));
}

TEST(DigestSchedule, UsesStandardTimeAfterDstEnds) {
  // DST ends Sunday 2026-11-01; 20:00 that evening is PST (UTC-8).
  TimePoint monday = utc(2026, 11, 2, 18);
  EXPECT_EQ(latestDigestSlot(monday, DigestConfig{}, pacific()), utc(2026, 11, 2, 4));
}

TEST(DigestSchedule, DueRightAfterSlotWhenNeverSent) {
  EXPECT_EQ(dueDigestSlot(kSundaySlot + 5min, std::nullopt, DigestConfig{}, pacific()),
            kSundaySlot);
}

TEST(DigestSchedule, NotDueOnceSlotWasSent) {
  EXPECT_EQ(dueDigestSlot(kSundaySlot + 5min, kSundaySlot, DigestConfig{}, pacific()),
            std::nullopt);
}

TEST(DigestSchedule, DueAgainNextWeek) {
  TimePoint next = kSundaySlot + std::chrono::days{7};
  EXPECT_EQ(dueDigestSlot(next + 1min, kSundaySlot, DigestConfig{}, pacific()), next);
}

TEST(DigestSchedule, MissedSlotIsSkippedAfterGracePeriod) {
  EXPECT_NE(
      dueDigestSlot(kSundaySlot + kDigestGracePeriod - 1s, std::nullopt, DigestConfig{}, pacific()),
      std::nullopt);
  EXPECT_EQ(
      dueDigestSlot(kSundaySlot + kDigestGracePeriod, std::nullopt, DigestConfig{}, pacific()),
      std::nullopt);
}

Launch launchAt(const std::string& id, TimePoint time) {
  Launch launch = makeLaunch();
  launch.sourceLaunchId = id;
  launch.launchTime = time;
  return launch;
}

std::vector<std::string> ids(const std::vector<Launch>& launches) {
  std::vector<std::string> result;
  for (const auto& launch : launches) result.push_back(launch.sourceLaunchId);
  return result;
}

TEST(DigestSelection, KeepsRelevantLaunchesInWindowSoonestFirst) {
  Launch later = launchAt("later", kNow + std::chrono::days{3});
  Launch sooner = launchAt("sooner", kNow + 2h);
  Launch past = launchAt("past", kNow - 1h);
  Launch beyond = launchAt("beyond", kNow + std::chrono::days{7});
  Launch no_time = launchAt("no-time", kNow);
  no_time.launchTime.reset();
  Launch ended = launchAt("ended", kNow + 1h);
  ended.status = LaunchStatus::kEnded;
  Launch other_provider = launchAt("rocket-lab", kNow + 1h);
  other_provider.provider = "Rocket Lab";
  Launch no_term = launchAt("no-term", kNow + 1h);
  no_term.name = "Falcon 9 Block 5 | Transporter-15";
  no_term.missionName = "Transporter-15";
  no_term.missionDescription.reset();

  auto selected =
      selectDigestLaunches({later, sooner, past, beyond, no_time, ended, other_provider, no_term},
                           "SpaceX", {"Starlink"}, kNow, std::chrono::days{7});
  EXPECT_THAT(ids(selected), ElementsAre("sooner", "later"));
}

TEST(DigestSelection, KeepsNoGoAndUnconfirmedLaunches) {
  Launch hold = launchAt("hold", kNow + 1h);
  hold.status = LaunchStatus::kNoGo;
  Launch tbd = launchAt("tbd", kNow + 2h);
  tbd.status = LaunchStatus::kUnknown;
  auto selected =
      selectDigestLaunches({hold, tbd}, "SpaceX", {"Starlink"}, kNow, std::chrono::days{7});
  EXPECT_THAT(ids(selected), ElementsAre("hold", "tbd"));
}

TEST(DigestStateStore, MissingFileMeansNeverSent) {
  TempDir dir;
  JsonDigestStateStore store(dir / "digest.json");
  EXPECT_EQ(store.loadLastSentSlot(), std::nullopt);
}

TEST(DigestStateStore, RoundTripsLastSentSlot) {
  TempDir dir;
  JsonDigestStateStore store(dir / "nested" / "digest.json");
  store.saveLastSentSlot(kSundaySlot);
  EXPECT_EQ(JsonDigestStateStore(store.path()).loadLastSentSlot(), kSundaySlot);
}

TEST(DigestStateStore, CorruptFileThrows) {
  TempDir dir;
  JsonDigestStateStore store(dir / "digest.json");
  std::ofstream(store.path()) << "{\"last_sent_slot\": \"yesterday\"}";
  EXPECT_THROW(store.loadLastSentSlot(), StorageError);
}

}  // namespace
}  // namespace rocket_watcher::testing
