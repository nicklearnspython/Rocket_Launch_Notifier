// Message formatting: turns Alerts into channel-independent messages.
//
// The formatter decides alert wording; delivery code never does. Launch
// times are rendered in the configured display timezone. Matched Include
// Terms are intentionally not shown in the notification body for v1.
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
