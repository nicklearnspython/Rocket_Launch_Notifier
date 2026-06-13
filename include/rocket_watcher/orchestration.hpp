// Single Watcher cycle orchestration, shared by `once` and `watch`.
//
// Order of operations per cycle: fetch -> decide -> save Alert Records ->
// notify. If saving Alert Records fails, no Notifications are sent for the
// cycle. Notification failure never rolls back a saved Alert Record.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "rocket_watcher/channel.hpp"
#include "rocket_watcher/config.hpp"
#include "rocket_watcher/source.hpp"
#include "rocket_watcher/storage.hpp"

namespace rocket_watcher {

using Logger = std::function<void(const std::string&)>;

struct CycleOutcome {
  bool sourceFailed = false;
  bool saveFailed = false;
  int alertsCreated = 0;
  std::vector<DeliveryResult> deliveries;

  bool anyDeliveryFailed() const;
  bool ok() const;
};

struct CycleOptions {
  std::function<TimePoint()> nowFn = utcNow;
  bool dryRun = false;
};

CycleOutcome runCycle(LaunchScheduleSource& source, AlertRecordStore& store,
                      NotificationChannel& channel, const Config& config, const Logger& log,
                      const CycleOptions& options = {});

}  // namespace rocket_watcher
