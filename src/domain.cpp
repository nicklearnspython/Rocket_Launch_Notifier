#include "rocket_watcher/domain.hpp"

#include <stdexcept>

namespace rocket_watcher {

TimePoint utcNow() {
  return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
}

std::string toString(TimingPrecision value) {
  switch (value) {
    case TimingPrecision::kPrecise:
      return "precise";
    case TimingPrecision::kHour:
      return "hour";
    case TimingPrecision::kImprecise:
      return "imprecise";
    case TimingPrecision::kUnknown:
      return "unknown";
  }
  throw std::invalid_argument("unhandled TimingPrecision");
}

std::string toString(LaunchStatus value) {
  switch (value) {
    case LaunchStatus::kGo:
      return "go";
    case LaunchStatus::kNoGo:
      return "no_go";
    case LaunchStatus::kEnded:
      return "ended";
    case LaunchStatus::kUnknown:
      return "unknown";
  }
  throw std::invalid_argument("unhandled LaunchStatus");
}

std::string toString(AlertKind value) {
  switch (value) {
    case AlertKind::kLaunchSoon:
      return "launch_soon";
    case AlertKind::kLaunchImminent:
      return "launch_imminent";
    case AlertKind::kHourPrecision:
      return "hour_precision";
    case AlertKind::kCorrection:
      return "correction";
  }
  throw std::invalid_argument("unhandled AlertKind");
}

std::string toString(CorrectionReason value) {
  switch (value) {
    case CorrectionReason::kNoGo:
      return "no_go";
    case CorrectionReason::kTimeChanged:
      return "time_changed";
    case CorrectionReason::kTimingNowImprecise:
      return "timing_now_imprecise";
  }
  throw std::invalid_argument("unhandled CorrectionReason");
}

TimingPrecision timingPrecisionFromString(const std::string& value) {
  if (value == "precise") return TimingPrecision::kPrecise;
  if (value == "hour") return TimingPrecision::kHour;
  if (value == "imprecise") return TimingPrecision::kImprecise;
  if (value == "unknown") return TimingPrecision::kUnknown;
  throw std::invalid_argument("unknown timing precision: " + value);
}

LaunchStatus launchStatusFromString(const std::string& value) {
  if (value == "go") return LaunchStatus::kGo;
  if (value == "no_go") return LaunchStatus::kNoGo;
  if (value == "ended") return LaunchStatus::kEnded;
  if (value == "unknown") return LaunchStatus::kUnknown;
  throw std::invalid_argument("unknown launch status: " + value);
}

AlertKind alertKindFromString(const std::string& value) {
  if (value == "launch_soon") return AlertKind::kLaunchSoon;
  if (value == "launch_imminent") return AlertKind::kLaunchImminent;
  if (value == "hour_precision") return AlertKind::kHourPrecision;
  if (value == "correction") return AlertKind::kCorrection;
  throw std::invalid_argument("unknown alert kind: " + value);
}

CorrectionReason correctionReasonFromString(const std::string& value) {
  if (value == "no_go") return CorrectionReason::kNoGo;
  if (value == "time_changed") return CorrectionReason::kTimeChanged;
  if (value == "timing_now_imprecise") return CorrectionReason::kTimingNowImprecise;
  throw std::invalid_argument("unknown correction reason: " + value);
}

std::vector<std::string> Launch::searchableFields() const {
  std::vector<std::string> fields;
  if (!name.empty()) fields.push_back(name);
  for (const auto& optional_field : {missionName, missionDescription, padName}) {
    if (optional_field && !optional_field->empty()) fields.push_back(*optional_field);
  }
  return fields;
}

}  // namespace rocket_watcher
