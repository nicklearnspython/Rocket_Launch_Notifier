// Single Watcher cycle orchestration, shared by `once` and `watch`.
//
// Order of operations per cycle: fetch -> decide -> save Alert Records ->
// notify. If saving Alert Records fails, no Notifications are sent for the
// cycle. Notification failure never rolls back a saved Alert Record.
//
// Logging is quiet by default: alerts, deliveries, and failures are always
// logged; tracked (relevant) launches are logged only when they appear,
// change, or drop out; and a one-line status is logged hourly. Verbose mode
// and dry runs log every launch evaluation instead.
#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <set>
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

inline constexpr Duration kStatusLogInterval = std::chrono::hours{1};

// What quiet-mode logging has already reported, carried across the cycles
// of one `watch` so that repeated, unchanged state is not logged again.
struct LogMemory {
  struct TrackedLaunch {
    std::string name;
    std::string note;
  };
  std::map<std::string, TrackedLaunch> tracked;  // launch key -> last logged state
  std::set<std::string> warnings;                // source warnings seen last fetch
  std::optional<TimePoint> lastStatusAt;
  int checksSinceStatus = 0;
  int failedChecksSinceStatus = 0;
};

struct CycleOptions {
  std::function<TimePoint()> nowFn = utcNow;
  bool dryRun = false;
  bool verbose = false;
  // Null means a fresh memory for this cycle only (e.g. `once`).
  LogMemory* memory = nullptr;
};

CycleOutcome runCycle(LaunchScheduleSource& source, AlertRecordStore& store,
                      NotificationChannel& channel, const Config& config, const Logger& log,
                      const CycleOptions& options = {});

}  // namespace rocket_watcher
