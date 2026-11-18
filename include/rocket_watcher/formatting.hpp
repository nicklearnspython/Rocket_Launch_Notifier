// Message formatting: turns Alerts into channel-independent messages.
//
// The formatter decides alert wording; delivery code never does. Titles use
// the mission part of the launch name so they fit on a phone; bodies open
// with the vehicle and launch site. Launch times are rendered in the
// configured display timezone. Matched Include Terms are intentionally not
// shown in the notification body.
#pragma once

#include <chrono>
#include <string>

#include "rocket_watcher/domain.hpp"

namespace rocket_watcher {

struct Message {
  std::string title;
  std::string body;
  std::string alertKind;
  std::string launchKey;
  std::string launchName;
};

Message formatAlert(const Alert& alert, const std::chrono::time_zone* displayZone);

}  // namespace rocket_watcher
