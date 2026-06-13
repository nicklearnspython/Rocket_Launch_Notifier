// Shared builders for domain objects, mirroring the defaults the whole
// suite assumes (a relevant SpaceX Starlink launch 45 minutes out).
#pragma once

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "rocket_watcher/config.hpp"
#include "rocket_watcher/domain.hpp"

namespace rocket_watcher::testing {

using namespace std::chrono_literals;

// 2026-06-09 18:00:00 UTC (11:00 PDT).
inline constexpr TimePoint kNow =
    std::chrono::sys_days{std::chrono::year{2026} / 6 / 9} + std::chrono::hours{18};

inline Launch makeLaunch() {
  Launch launch;
  launch.sourceName = "ll2";
  launch.sourceLaunchId = "abc-123";
  launch.name = "Falcon 9 Block 5 | Starlink Group 12-34";
  launch.provider = "SpaceX";
  launch.launchTime = kNow + 45min;
  launch.precision = TimingPrecision::kPrecise;
  launch.status = LaunchStatus::kGo;
  launch.missionName = "Starlink Group 12-34";
  launch.missionDescription = "A batch of Starlink satellites.";
  launch.padName = "SLC-40, Cape Canaveral";
  return launch;
}

inline AlertPolicy makePolicy() {
  return AlertPolicy{60min, 10min, 30min, 30h, std::chrono::days{30}};
}

inline AlertRecord makeRecord(const Launch& launch, const std::vector<SentAlert>& sentAlerts,
                              TimePoint lastAlertAt = kNow) {
  AlertRecord record;
  record.key = launch.key();
  record.sourceName = launch.sourceName;
  record.sourceLaunchId = launch.sourceLaunchId;
  record.launchName = launch.name;
  record.matchedTerms = {"Starlink"};
  record.lastFacts = LaunchFacts{launch.launchTime, launch.precision, launch.status};
  record.sentAlerts = sentAlerts;
  record.firstAlertAt = lastAlertAt;
  record.lastAlertAt = lastAlertAt;
  return record;
}

inline AlertRecord makeRecord(const Launch& launch, const std::vector<AlertKind>& sentKinds,
                              TimePoint lastAlertAt = kNow) {
  std::vector<SentAlert> sent;
  for (AlertKind kind : sentKinds) {
    sent.push_back(SentAlert{kind, lastAlertAt, std::nullopt});
  }
  return makeRecord(launch, sent, lastAlertAt);
}

inline Config makeConfig() {
  Config config;
  config.launchProvider = "SpaceX";
  config.includeTerms = {"Starlink"};
  config.recipients = {{"alice", "PUSHOVER_USER_ALICE"}, {"bob", "PUSHOVER_USER_BOB"}};
  config.pushoverTokenEnv = "PUSHOVER_TOKEN";
  return config;
}

// Creates a unique temporary directory and removes it on destruction.
class TempDir {
 public:
  TempDir() {
    std::random_device device;
    std::uniform_int_distribution<uint64_t> distribution;
    path_ = std::filesystem::temp_directory_path() /
            ("rocket_watcher_test_" + std::to_string(distribution(device)));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::filesystem::path& path() const { return path_; }
  std::filesystem::path operator/(const std::string& name) const { return path_ / name; }

 private:
  std::filesystem::path path_;
};

}  // namespace rocket_watcher::testing
