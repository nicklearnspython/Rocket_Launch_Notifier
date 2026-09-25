#include "rocket_watcher/formatting.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "rocket_watcher/strings.hpp"
#include "rocket_watcher/time_utils.hpp"

namespace rocket_watcher {

namespace {

// Separates vehicle from mission in source launch names, as in
// "Falcon 9 Block 5 | Starlink Group 17-44".
constexpr std::string_view kNameSeparator = " | ";

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

// The mission part of the launch name, which fits a phone-width title.
std::string shortName(const Launch& launch) {
  size_t at = launch.name.find(kNameSeparator);
  if (at == std::string::npos || at + kNameSeparator.size() >= launch.name.size()) {
    return launch.name;
  }
  return launch.name.substr(at + kNameSeparator.size());
}

std::optional<std::string> vehicleName(const Launch& launch) {
  if (launch.vehicle) return launch.vehicle;
  size_t at = launch.name.find(kNameSeparator);
  if (at == std::string::npos || at == 0) return std::nullopt;
  return launch.name.substr(0, at);
}

// "Space Launch Complex 4E, Vandenberg SFB, CA, USA" -> "SLC-4E, Vandenberg SFB, CA".
std::string shortPad(std::string pad) {
  const std::string slc = "Space Launch Complex ";
  const std::string lc = "Launch Complex ";
  const std::string usa = ", USA";
  if (startsWith(pad, slc)) {
    pad = "SLC-" + pad.substr(slc.size());
  } else if (startsWith(pad, lc)) {
    pad = "LC-" + pad.substr(lc.size());
  }
  if (pad.size() > usa.size() && pad.ends_with(usa)) {
    pad.resize(pad.size() - usa.size());
  }
  return pad;
}

// "Falcon 9 from SLC-4E, Vandenberg SFB, CA\n", or less when facts are missing.
std::string vehicleLine(const Launch& launch) {
  std::optional<std::string> vehicle = vehicleName(launch);
  if (vehicle && launch.padName) return *vehicle + " from " + shortPad(*launch.padName) + "\n";
  if (vehicle) return *vehicle + "\n";
  if (launch.padName) return "From " + shortPad(*launch.padName) + "\n";
  return "";
}

Message formatCorrection(const Alert& alert, const std::chrono::time_zone* displayZone) {
  const std::string& name = alert.launch.name;
  std::string short_name = shortName(alert.launch);
  std::optional<TimePoint> previous_time =
      alert.previous ? alert.previous->launchTime : std::nullopt;
  std::string was = formatDisplay(previous_time, displayZone);

  std::string title;
  std::string body;
  if (alert.reason == CorrectionReason::kNoGo) {
    title = "Launch no longer go: " + short_name;
    body = "No longer go for launch. Was expected at " + was + ".";
  } else if (alert.reason == CorrectionReason::kTimeChanged) {
    title = "Launch time changed: " + short_name;
    std::string now_when = formatDisplay(alert.launch.launchTime, displayZone);
    body = "New estimated liftoff: " + now_when + ". Was " + was + ".";
  } else if (alert.reason == CorrectionReason::kTimingNowImprecise) {
    title = "Launch timing now uncertain: " + short_name;
    body = "Launch timing is no longer precise. Last estimate was " + was + ".";
  } else {
    throw std::invalid_argument("correction alert without a known reason");
  }

  return Message{title, vehicleLine(alert.launch) + body, "correction:" + toString(*alert.reason),
                 alert.launch.key(), name};
}

std::string digestStatus(LaunchStatus status) {
  switch (status) {
    case LaunchStatus::kGo:
      return "go";
    case LaunchStatus::kNoGo:
      return "on hold";
    case LaunchStatus::kEnded:
      return "ended";
    case LaunchStatus::kUnknown:
      break;
  }
  return "not yet confirmed";
}

// "Tue Sep 29, 15:30 PDT", "Tue Sep 29, around 15:00 PDT", or
// "Tue Sep 29 (date tentative)" depending on Timing Precision.
std::string digestWhen(const Launch& launch, const std::chrono::time_zone* displayZone) {
  if (!launch.launchTime) return "date unknown";
  TimePoint time = *launch.launchTime;
  std::string day = formatDisplayWeekday(time, displayZone);
  switch (launch.precision) {
    case TimingPrecision::kPrecise:
      return day + ", " + formatDisplayShort(time, displayZone);
    case TimingPrecision::kHour:
      return day + ", around " + formatDisplayShortHour(time, displayZone);
    case TimingPrecision::kImprecise:
    case TimingPrecision::kUnknown:
      break;
  }
  return day + " (date tentative)";
}

// "Flight 12\nTue Sep 29, 15:30 PDT · go\nStarship from Starbase, TX\n"
std::string digestEntry(const Launch& launch, const std::chrono::time_zone* displayZone) {
  return shortName(launch) + "\n" + digestWhen(launch, displayZone) + " · " +
         digestStatus(launch.status) + "\n" + vehicleLine(launch);
}

}  // namespace

Message formatAlert(const Alert& alert, const std::chrono::time_zone* displayZone) {
  const std::string& name = alert.launch.name;
  std::string short_name = shortName(alert.launch);
  std::string when = formatDisplay(alert.launch.launchTime, displayZone);

  std::string title;
  std::string body;
  switch (alert.kind) {
    case AlertKind::kLaunchSoon:
      title = "Launch soon: " + short_name;
      body = "Estimated liftoff: " + when + " (~" + std::to_string(minutesAway(alert)) + " min).";
      break;
    case AlertKind::kLaunchImminent:
      title = "Launch imminent: " + short_name;
      body = "Liftoff expected at " + when + " (~" + std::to_string(minutesAway(alert)) +
             " min). Open the live feed now.";
      break;
    case AlertKind::kHourPrecision:
      title = "Possible launch this hour: " + short_name;
      body = "Timing is only precise to the hour; estimated window: " +
             formatDisplayHour(requiredLaunchTime(alert), displayZone) + ".";
      break;
    case AlertKind::kCorrection:
      return formatCorrection(alert, displayZone);
  }

  return Message{title, vehicleLine(alert.launch) + body, toString(alert.kind), alert.launch.key(),
                 name};
}

Message formatDigest(const std::vector<Launch>& launches, int lookaheadDays,
                     const std::chrono::time_zone* displayZone) {
  std::string period =
      (lookaheadDays == 7) ? "This week" : "Next " + std::to_string(lookaheadDays) + " days";
  if (launches.empty()) {
    return Message{period + ": no tracked launches",
                   "No tracked launches in the next " + std::to_string(lookaheadDays) +
                       " days. Countdown alerts still go out if one is scheduled.",
                   "weekly_digest", "digest", "weekly digest"};
  }

  const std::string footer = "Times often slip; countdown alerts will follow.";
  std::string title = period + ": " + std::to_string(launches.size()) + " tracked launch" +
                      (launches.size() == 1 ? "" : "es");
  auto more_line = [](size_t count) { return "...and " + std::to_string(count) + " more\n\n"; };
  std::string body;
  for (size_t i = 0; i < launches.size(); ++i) {
    std::string entry = digestEntry(launches[i], displayZone) + "\n";
    size_t left_after = launches.size() - i - 1;
    // Keep room for a later "...and N more" line unless this is the last entry.
    size_t reserve = (left_after > 0) ? more_line(left_after).size() : 0;
    if (body.size() + entry.size() + reserve + footer.size() > kMaxMessageBodyLength) {
      body += more_line(launches.size() - i);
      break;
    }
    body += entry;
  }
  return Message{title, body + footer, "weekly_digest", "digest", "weekly digest"};
}

}  // namespace rocket_watcher
