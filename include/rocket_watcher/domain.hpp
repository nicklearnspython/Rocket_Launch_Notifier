// Project-level domain model for the Watcher.
//
// Everything in this header is pure data: no IO, no source- or
// channel-specific vocabulary. Timestamps are UTC.
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace rocket_watcher {

// All timestamps are UTC with second precision.
using TimePoint = std::chrono::sys_seconds;
using Duration = std::chrono::seconds;

TimePoint utcNow();

enum class TimingPrecision { kPrecise, kHour, kImprecise, kUnknown };
enum class LaunchStatus { kGo, kNoGo, kEnded, kUnknown };
enum class AlertKind { kLaunchSoon, kLaunchImminent, kHourPrecision, kCorrection };
enum class CorrectionReason { kNoGo, kTimeChanged, kTimingNowImprecise };

std::string toString(TimingPrecision value);
std::string toString(LaunchStatus value);
std::string toString(AlertKind value);
std::string toString(CorrectionReason value);

// Each throws std::invalid_argument for an unrecognized value string.
TimingPrecision timingPrecisionFromString(const std::string& value);
LaunchStatus launchStatusFromString(const std::string& value);
AlertKind alertKindFromString(const std::string& value);
CorrectionReason correctionReasonFromString(const std::string& value);

// A normalized launch per the Launch Schedule Source Contract.
struct Launch {
  std::string sourceName;
  std::string sourceLaunchId;
  std::string name;
  std::string provider;
  std::optional<TimePoint> launchTime;
  TimingPrecision precision = TimingPrecision::kUnknown;
  LaunchStatus status = LaunchStatus::kUnknown;
  std::optional<std::string> missionName;
  std::optional<std::string> missionDescription;
  std::optional<std::string> padName;

  // Combined stable identity key, backed by the source.
  std::string key() const { return sourceName + ":" + sourceLaunchId; }

  std::vector<std::string> searchableFields() const;

  bool operator==(const Launch&) const = default;
};

// Correction-relevant snapshot of a launch at alert time.
struct LaunchFacts {
  std::optional<TimePoint> launchTime;
  TimingPrecision precision = TimingPrecision::kUnknown;
  LaunchStatus status = LaunchStatus::kUnknown;

  bool operator==(const LaunchFacts&) const = default;
};

// A semantic alert decision produced by the decision core.
struct Alert {
  AlertKind kind = AlertKind::kLaunchSoon;
  Launch launch;
  TimePoint createdAt;
  std::vector<std::string> matchedTerms;
  std::optional<CorrectionReason> reason;
  std::optional<LaunchFacts> previous;

  bool operator==(const Alert&) const = default;
};

struct SentAlert {
  AlertKind kind = AlertKind::kLaunchSoon;
  TimePoint createdAt;
  std::optional<CorrectionReason> reason;

  bool operator==(const SentAlert&) const = default;
};

// Persistent memory of alert decisions for one launch.
//
// Records exist only for launches that produced at least one Alert;
// the Watcher is not a local launch database.
struct AlertRecord {
  std::string key;
  std::string sourceName;
  std::string sourceLaunchId;
  std::string launchName;
  std::vector<std::string> matchedTerms;
  LaunchFacts lastFacts;
  std::vector<SentAlert> sentAlerts;
  TimePoint firstAlertAt;
  TimePoint lastAlertAt;

  bool operator==(const AlertRecord&) const = default;
};

// Pure policy inputs for the decision core.
struct AlertPolicy {
  Duration launchSoon{};
  Duration launchImminent{};
  Duration correctionTimeShift{};
  Duration scheduleLookahead{};
  Duration retention{};
};

}  // namespace rocket_watcher
