#include "rocket_watcher/orchestration.hpp"

#include <algorithm>
#include <ranges>

#include "rocket_watcher/core.hpp"
#include "rocket_watcher/strings.hpp"

namespace rocket_watcher {

namespace {

void logWarnings(const std::vector<std::string>& warnings, bool verbose, LogMemory& memory,
                 const Logger& log) {
  std::set<std::string> current(warnings.begin(), warnings.end());
  for (const auto& warning : warnings) {
    if (verbose || !memory.warnings.contains(warning)) {
      log("source warning: " + warning);
    }
  }
  memory.warnings = std::move(current);
}

// Logs relevant launches as they start being tracked, change state, or drop
// out, and records what was logged. Alerted launches are skipped: their
// "alert created" line already says what happened.
void logTrackingChanges(const std::vector<Evaluation>& evaluations, bool quiet, LogMemory& memory,
                        const Logger& log) {
  std::map<std::string, LogMemory::TrackedLaunch> current;
  std::set<std::string> fetched;
  for (const auto& evaluation : evaluations) {
    fetched.insert(evaluation.launchKey);
    if (!evaluation.relevant) continue;
    auto previous = memory.tracked.find(evaluation.launchKey);
    if (evaluation.alerted) {
      if (previous != memory.tracked.end()) current.insert(*previous);
      continue;
    }
    if (quiet) {
      if (previous == memory.tracked.end()) {
        log("now tracking: " + evaluation.launchName + ": " + evaluation.note);
      } else if (previous->second.note != evaluation.note) {
        log("tracking update: " + evaluation.launchName + ": " + previous->second.note + " -> " +
            evaluation.note);
      }
    }
    current[evaluation.launchKey] = {evaluation.launchName, evaluation.note};
  }
  if (quiet) {
    for (const auto& [key, launch] : memory.tracked) {
      if (current.contains(key)) continue;
      log("no longer tracking: " + launch.name +
          (fetched.contains(key) ? " (no longer relevant)" : " (left the upcoming schedule)"));
    }
  }
  memory.tracked = std::move(current);
}

void logStatusIfDue(size_t launchCount, const std::vector<Evaluation>& evaluations,
                    LogMemory& memory, TimePoint now, const Logger& log) {
  if (memory.lastStatusAt && now - *memory.lastStatusAt < kStatusLogInterval) return;
  auto tracked = std::ranges::count_if(evaluations, &Evaluation::relevant);
  log("status: " + std::to_string(launchCount) + " upcoming launches evaluated, " +
      std::to_string(tracked) + " tracked; " + std::to_string(memory.checksSinceStatus) +
      " check(s) since last status, " + std::to_string(memory.failedChecksSinceStatus) + " failed");
  memory.lastStatusAt = now;
  memory.checksSinceStatus = 0;
  memory.failedChecksSinceStatus = 0;
}

// Sends the Weekly Digest if one is due. Failures are logged and reported
// in `outcome`; they never affect the cycle's Alerts.
void sendDigestIfDue(const std::vector<Launch>& launches, DigestStateStore& store,
                     NotificationChannel& channel, const Config& config, const Logger& log,
                     TimePoint now, bool dryRun, CycleOutcome& outcome) {
  std::optional<TimePoint> last_sent;
  try {
    last_sent = store.loadLastSentSlot();
  } catch (const StorageError& error) {
    // Without the last sent slot, a digest could repeat every cycle.
    log(std::string("failed to load digest state: ") + error.what() + "; skipping weekly digest");
    outcome.saveFailed = true;
    return;
  }
  std::optional<TimePoint> slot =
      dueDigestSlot(now, last_sent, config.digest, config.displayZone());
  if (!slot) return;

  Message message = buildDigest(launches, config, now);
  if (dryRun) {
    std::string body = message.body;
    std::ranges::replace(body, '\n', ' ');
    log("dry run: weekly digest due; would send [" + message.title + "] " + body);
    return;
  }
  try {
    store.saveLastSentSlot(*slot);
  } catch (const StorageError& error) {
    log(std::string("failed to save digest state: ") + error.what() +
        "; not sending weekly digest");
    outcome.saveFailed = true;
    return;
  }
  log("weekly digest: [" + message.title + "]");
  auto deliveries = deliverToAll(channel, config, message, log);
  outcome.deliveries.insert(outcome.deliveries.end(), deliveries.begin(), deliveries.end());
  outcome.digestSent = true;
}

}  // namespace

std::vector<DeliveryResult> deliverToAll(NotificationChannel& channel, const Config& config,
                                         const Message& message, const Logger& log) {
  std::vector<DeliveryResult> results;
  for (const auto& recipient : config.recipients) {
    DeliveryResult result = channel.send(message, recipient.name);
    std::string status = result.ok ? "delivered" : "DELIVERY FAILED";
    log(status + " to " + recipient.name + ": " + result.detail + " [" + message.title + "]");
    results.push_back(result);
  }
  return results;
}

Message buildDigest(const std::vector<Launch>& launches, const Config& config, TimePoint now) {
  std::vector<Launch> selected =
      selectDigestLaunches(launches, config.launchProvider, config.includeTerms, now,
                           std::chrono::days{config.digest.lookaheadDays});
  return formatDigest(selected, config.digest.lookaheadDays, config.displayZone());
}

bool CycleOutcome::anyDeliveryFailed() const {
  return std::ranges::any_of(deliveries,
                             [](const DeliveryResult& delivery) { return !delivery.ok; });
}

bool CycleOutcome::ok() const { return !(sourceFailed || saveFailed || anyDeliveryFailed()); }

CycleOutcome runCycle(LaunchScheduleSource& source, AlertRecordStore& store,
                      NotificationChannel& channel, const Config& config, const Logger& log,
                      const CycleOptions& options) {
  CycleOutcome outcome;
  TimePoint now = options.nowFn();
  LogMemory fresh_memory;
  LogMemory& memory = (options.memory != nullptr) ? *options.memory : fresh_memory;
  bool log_every_evaluation = options.verbose || options.dryRun;
  DigestStateStore* digest_store = config.digest.enabled ? options.digestStore : nullptr;
  ++memory.checksSinceStatus;

  FetchResult fetch;
  try {
    fetch = source.fetch();
  } catch (const std::exception& error) {
    log(std::string("source fetch failed: ") + error.what());
    ++memory.failedChecksSinceStatus;
    outcome.sourceFailed = true;
    return outcome;
  }
  logWarnings(fetch.warnings, log_every_evaluation, memory, log);
  if (log_every_evaluation) {
    log("fetched " + std::to_string(fetch.launches.size()) + " upcoming launches");
  }

  std::vector<AlertRecord> records;
  try {
    records = store.load();
  } catch (const StorageError& error) {
    // Without prior Alert Records the core would re-create old Alerts,
    // so a load failure is treated like a save failure: no Notifications.
    log(std::string("failed to load alert records: ") + error.what() +
        "; sending no notifications this cycle");
    ++memory.failedChecksSinceStatus;
    outcome.saveFailed = true;
    return outcome;
  }

  CycleDecision decision = decideCycle(fetch.launches, records, config.policy(),
                                       config.launchProvider, config.includeTerms, now);
  if (log_every_evaluation) {
    for (const auto& evaluation : decision.evaluations) {
      log("eval: " + evaluation.line());
    }
  }
  logTrackingChanges(decision.evaluations, /*quiet=*/!log_every_evaluation, memory, log);
  logStatusIfDue(fetch.launches.size(), decision.evaluations, memory, now, log);
  outcome.alertsCreated = static_cast<int>(decision.alerts.size());

  const std::chrono::time_zone* display_zone = config.displayZone();
  std::vector<std::pair<Alert, Message>> messages;
  messages.reserve(decision.alerts.size());
  for (const auto& alert : decision.alerts) {
    messages.emplace_back(alert, formatAlert(alert, display_zone));
  }

  if (options.dryRun) {
    if (messages.empty()) {
      log("dry run: no alerts would be created this cycle");
    }
    for (const auto& [alert, message] : messages) {
      std::string body = message.body;
      std::ranges::replace(body, '\n', ' ');
      log("dry run: would send [" + message.title + "] " + body +
          " (matched: " + join(alert.matchedTerms, ", ") + ")");
    }
    log("dry run: would persist " + std::to_string(decision.updatedRecords.size()) +
        " alert record(s); no notifications sent, no records or logs written");
    if (digest_store != nullptr) {
      sendDigestIfDue(fetch.launches, *digest_store, channel, config, log, now, true, outcome);
    }
    return outcome;
  }

  bool records_changed =
      !decision.alerts.empty() || decision.updatedRecords.size() != records.size();
  if (records_changed) {
    try {
      store.save(decision.updatedRecords);
    } catch (const StorageError& error) {
      log(std::string("failed to save alert records: ") + error.what() +
          "; sending no notifications this cycle");
      ++memory.failedChecksSinceStatus;
      outcome.saveFailed = true;
      return outcome;
    }
    log("saved " + std::to_string(decision.updatedRecords.size()) + " alert record(s)");
  }

  for (const auto& [alert, message] : messages) {
    log("alert created: [" + message.title + "] (matched: " + join(alert.matchedTerms, ", ") + ")");
    auto deliveries = deliverToAll(channel, config, message, log);
    outcome.deliveries.insert(outcome.deliveries.end(), deliveries.begin(), deliveries.end());
  }

  if (digest_store != nullptr) {
    sendDigestIfDue(fetch.launches, *digest_store, channel, config, log, now, false, outcome);
  }
  return outcome;
}

}  // namespace rocket_watcher
