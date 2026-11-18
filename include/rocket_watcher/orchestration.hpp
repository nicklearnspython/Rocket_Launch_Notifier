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
//
// After alerts, a cycle also sends the Weekly Digest when one is due (see
// digest.hpp), reusing the cycle's fetch. Like Alert Records, the digest
// slot is saved before sending, so a failed save sends nothing and a failed
// delivery is not retried.
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
#include "rocket_watcher/digest.hpp"
#include "rocket_watcher/source.hpp"
#include "rocket_watcher/storage.hpp"

namespace rocket_watcher {

using Logger = std::function<void(const std::string&)>;

struct CycleOutcome {
  bool sourceFailed = false;
  bool saveFailed = false;
  int alertsCreated = 0;
  bool digestSent = false;
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
  // Null disables the Weekly Digest for this cycle.
  DigestStateStore* digestStore = nullptr;
};

CycleOutcome runCycle(LaunchScheduleSource& source, AlertRecordStore& store,
                      NotificationChannel& channel, const Config& config, const Logger& log,
                      const CycleOptions& options = {});

// Sends `message` to every recipient, logging each outcome.
std::vector<DeliveryResult> deliverToAll(NotificationChannel& channel, const Config& config,
                                         const Message& message, const Logger& log);

// The Weekly Digest for `launches` as of `now`, per config.
Message buildDigest(const std::vector<Launch>& launches, const Config& config, TimePoint now);

}  // namespace rocket_watcher
