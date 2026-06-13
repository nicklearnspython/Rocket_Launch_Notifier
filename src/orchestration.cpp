#include "rocket_watcher/orchestration.hpp"

#include <algorithm>
#include <ranges>

#include "rocket_watcher/core.hpp"
#include "rocket_watcher/strings.hpp"

namespace rocket_watcher {

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

  FetchResult fetch;
  try {
    fetch = source.fetch();
  } catch (const std::exception& error) {
    log(std::string("source fetch failed: ") + error.what());
    outcome.sourceFailed = true;
    return outcome;
  }
  for (const auto& warning : fetch.warnings) {
    log("source warning: " + warning);
  }
  log("fetched " + std::to_string(fetch.launches.size()) + " upcoming launches");

  std::vector<AlertRecord> records;
  try {
    records = store.load();
  } catch (const StorageError& error) {
    // Without prior Alert Records the core would re-create old Alerts,
    // so a load failure is treated like a save failure: no Notifications.
    log(std::string("failed to load alert records: ") + error.what() +
        "; sending no notifications this cycle");
    outcome.saveFailed = true;
    return outcome;
  }

  CycleDecision decision = decideCycle(fetch.launches, records, config.policy(),
                                       config.launchProvider, config.includeTerms, now);
  for (const auto& line : decision.evaluations) {
    log("eval: " + line);
  }
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
      log("dry run: would send [" + message.title + "] " + message.body +
          " (matched: " + join(alert.matchedTerms, ", ") + ")");
    }
    log("dry run: would persist " + std::to_string(decision.updatedRecords.size()) +
        " alert record(s); no notifications sent, no records or logs written");
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
      outcome.saveFailed = true;
      return outcome;
    }
    log("saved " + std::to_string(decision.updatedRecords.size()) + " alert record(s)");
  }

  for (const auto& [alert, message] : messages) {
    log("alert created: [" + message.title + "] (matched: " + join(alert.matchedTerms, ", ") + ")");
    for (const auto& recipient : config.recipients) {
      DeliveryResult result = channel.send(message, recipient.name);
      outcome.deliveries.push_back(result);
      std::string status = result.ok ? "delivered" : "DELIVERY FAILED";
      log(status + " to " + recipient.name + ": " + result.detail + " [" + message.title + "]");
    }
  }
  return outcome;
}

}  // namespace rocket_watcher
