#include "rocket_watcher/formatting.hpp"

#include <algorithm>
#include <stdexcept>

#include "rocket_watcher/time_utils.hpp"

namespace rocket_watcher {

namespace {

// Countdown and hour-precision alerts are only created for launches with
// a known launch time; corrections are formatted separately.
TimePoint requiredLaunchTime(const Alert& alert) {
  if (!alert.launch.launchTime) {
    throw std::invalid_argument("countdown alert requires a launch time");
  }
  return *alert.launch.launchTime;
}

long minutesAway(const Alert& alert) {
  auto remaining =
      std::chrono::floor<std::chrono::minutes>(requiredLaunchTime(alert) - alert.createdAt);
  return std::max<long>(0, remaining.count());
}

Message formatCorrection(const Alert& alert, const std::chrono::time_zone* displayZone) {
  const std::string& name = alert.launch.name;
  std::optional<TimePoint> previous_time =
      alert.previous ? alert.previous->launchTime : std::nullopt;
  std::string was = formatDisplay(previous_time, displayZone);

  std::string title;
  std::string body;
  if (alert.reason == CorrectionReason::kNoGo) {
    title = "Launch no longer go: " + name;
    body = name + " is no longer go for launch. Was expected at " + was + ".";
  } else if (alert.reason == CorrectionReason::kTimeChanged) {
    title = "Launch time changed: " + name;
    std::string now_when = formatDisplay(alert.launch.launchTime, displayZone);
    body = "New estimated liftoff: " + now_when + ". Was " + was + ".";
  } else if (alert.reason == CorrectionReason::kTimingNowImprecise) {
    title = "Launch timing now uncertain: " + name;
    body = "Launch timing is no longer precise. Last estimate was " + was + ".";
  } else {
    throw std::invalid_argument("correction alert without a known reason");
  }

  return Message{title, body, "correction:" + toString(*alert.reason), alert.launch.key(), name};
}

}  // namespace

Message formatAlert(const Alert& alert, const std::chrono::time_zone* displayZone) {
  const std::string& name = alert.launch.name;
  std::string when = formatDisplay(alert.launch.launchTime, displayZone);

  std::string title;
  std::string body;
  switch (alert.kind) {
    case AlertKind::kLaunchSoon:
      title = "Launch soon: " + name;
      body = "Estimated liftoff: " + when + " (~" + std::to_string(minutesAway(alert)) + " min).";
      break;
    case AlertKind::kLaunchImminent:
      title = "Launch imminent: " + name;
      body = "Liftoff expected at " + when + " (~" + std::to_string(minutesAway(alert)) +
             " min). Open the live feed now.";
      break;
    case AlertKind::kHourPrecision:
      title = "Possible launch this hour: " + name;
      body = "Possible launch this hour. Timing is only precise to the hour; estimated window: " +
             formatDisplayHour(requiredLaunchTime(alert), displayZone) + ".";
      break;
    case AlertKind::kCorrection:
      return formatCorrection(alert, displayZone);
  }

  return Message{title, body, toString(alert.kind), alert.launch.key(), name};
}

}  // namespace rocket_watcher
