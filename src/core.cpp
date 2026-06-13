#include "rocket_watcher/core.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "rocket_watcher/relevance.hpp"

namespace rocket_watcher {

namespace {

// An hour-precision launch time marks an hour-sized window, so the alert
// is eligible while "this hour" plausibly contains the launch.
constexpr Duration kHourWindow = std::chrono::hours{1};

Alert makeCountdown(const Launch& launch, AlertKind kind,
                    const std::vector<std::string>& matchedTerms, TimePoint now) {
  Alert alert;
  alert.kind = kind;
  alert.launch = launch;
  alert.createdAt = now;
  alert.matchedTerms = matchedTerms;
  return alert;
}

Alert makeCorrection(const Launch& launch, const AlertRecord& record, CorrectionReason reason,
                     const std::vector<std::string>& matchedTerms, TimePoint now) {
  Alert alert;
  alert.kind = AlertKind::kCorrection;
  alert.launch = launch;
  alert.createdAt = now;
  alert.matchedTerms = matchedTerms;
  alert.reason = reason;
  alert.previous = record.lastFacts;
  return alert;
}

struct LaunchDecision {
  std::optional<Alert> alert;
  std::string note;
};

LaunchDecision decideForLaunch(const Launch& launch, const AlertRecord* record,
                               const std::vector<std::string>& matchedTerms,
                               const AlertPolicy& policy, TimePoint now) {
  std::set<AlertKind> sent_kinds;
  if (record != nullptr) {
    for (const auto& sent : record->sentAlerts) sent_kinds.insert(sent.kind);
  }

  if (launch.status == LaunchStatus::kEnded || launch.status == LaunchStatus::kUnknown) {
    return {std::nullopt, "silent (status " + toString(launch.status) + ")"};
  }

  if (launch.status == LaunchStatus::kNoGo) {
    if (record == nullptr) {
      return {std::nullopt, "silent (no go, no prior alert to correct)"};
    }
    if (record->lastFacts.status == LaunchStatus::kNoGo) {
      return {std::nullopt, "silent (no go already corrected)"};
    }
    return {makeCorrection(launch, *record, CorrectionReason::kNoGo, matchedTerms, now),
            "correction alert (no go)"};
  }

  // Status is GO. Corrections are considered before new countdown alerts
  // and are not subject to the schedule lookahead: a meaningful change to
  // an already-alerted launch matters even if it moved far out.
  if (record != nullptr) {
    const LaunchFacts& prev = record->lastFacts;
    bool was_timed =
        prev.precision == TimingPrecision::kPrecise || prev.precision == TimingPrecision::kHour;
    bool now_untimed = launch.precision == TimingPrecision::kImprecise ||
                       launch.precision == TimingPrecision::kUnknown;
    if (was_timed && now_untimed) {
      return {
          makeCorrection(launch, *record, CorrectionReason::kTimingNowImprecise, matchedTerms, now),
          "correction alert (timing now imprecise)"};
    }
    if (prev.launchTime && launch.launchTime) {
      Duration shift = *launch.launchTime >= *prev.launchTime
                           ? *launch.launchTime - *prev.launchTime
                           : *prev.launchTime - *launch.launchTime;
      if (shift >= policy.correctionTimeShift) {
        return {makeCorrection(launch, *record, CorrectionReason::kTimeChanged, matchedTerms, now),
                "correction alert (time changed)"};
      }
    }
  }

  if (launch.precision == TimingPrecision::kImprecise ||
      launch.precision == TimingPrecision::kUnknown) {
    return {std::nullopt, "silent (timing " + toString(launch.precision) + ")"};
  }
  if (!launch.launchTime) {
    return {std::nullopt, "silent (no launch time)"};
  }

  Duration time_left = *launch.launchTime - now;
  if (time_left > policy.scheduleLookahead) {
    return {std::nullopt, "silent (beyond schedule lookahead)"};
  }

  if (launch.precision == TimingPrecision::kPrecise) {
    if (time_left < Duration::zero()) {
      return {std::nullopt, "silent (launch time already passed)"};
    }
    if (time_left <= policy.launchImminent && !sent_kinds.contains(AlertKind::kLaunchImminent)) {
      return {makeCountdown(launch, AlertKind::kLaunchImminent, matchedTerms, now),
              "launch imminent alert"};
    }
    if (time_left <= policy.launchSoon && !sent_kinds.contains(AlertKind::kLaunchSoon) &&
        !sent_kinds.contains(AlertKind::kLaunchImminent)) {
      return {makeCountdown(launch, AlertKind::kLaunchSoon, matchedTerms, now),
              "launch soon alert"};
    }
    return {std::nullopt, "no unsent countdown alert eligible"};
  }

  // Hour precision.
  if (-kHourWindow <= time_left && time_left <= kHourWindow &&
      !sent_kinds.contains(AlertKind::kHourPrecision)) {
    return {makeCountdown(launch, AlertKind::kHourPrecision, matchedTerms, now),
            "hour-precision alert"};
  }
  return {std::nullopt, "no hour-precision alert eligible"};
}

AlertRecord updatedRecord(const AlertRecord* record, const Launch& launch, const Alert& alert,
                          TimePoint now) {
  SentAlert sent{alert.kind, now, alert.reason};
  LaunchFacts facts{launch.launchTime, launch.precision, launch.status};
  if (record == nullptr) {
    AlertRecord created;
    created.key = launch.key();
    created.sourceName = launch.sourceName;
    created.sourceLaunchId = launch.sourceLaunchId;
    created.launchName = launch.name;
    created.matchedTerms = alert.matchedTerms;
    created.lastFacts = facts;
    created.sentAlerts = {sent};
    created.firstAlertAt = now;
    created.lastAlertAt = now;
    return created;
  }
  AlertRecord updated = *record;
  updated.launchName = launch.name;
  updated.matchedTerms = alert.matchedTerms;
  updated.lastFacts = facts;
  updated.sentAlerts.push_back(sent);
  updated.lastAlertAt = now;
  return updated;
}

}  // namespace

CycleDecision decideCycle(const std::vector<Launch>& launches,
                          const std::vector<AlertRecord>& records, const AlertPolicy& policy,
                          const std::string& launchProvider,
                          const std::vector<std::string>& includeTerms, TimePoint now) {
  std::map<std::string, AlertRecord> record_map;
  for (const auto& record : records) record_map.emplace(record.key, record);

  CycleDecision decision;
  for (const auto& launch : launches) {
    std::string label = launch.name + " [" + launch.key() + "]";
    RelevanceResult relevance = evaluateRelevance(launch, launchProvider, includeTerms);
    if (!relevance.relevant) {
      decision.evaluations.push_back(label + ": not relevant (" + relevance.reason + ")");
      continue;
    }
    auto found = record_map.find(launch.key());
    const AlertRecord* record = (found != record_map.end()) ? &found->second : nullptr;
    LaunchDecision result = decideForLaunch(launch, record, relevance.matchedTerms, policy, now);
    decision.evaluations.push_back(label + ": " + result.note);
    if (result.alert) {
      decision.alerts.push_back(*result.alert);
      record_map.insert_or_assign(launch.key(), updatedRecord(record, launch, *result.alert, now));
    }
  }

  for (const auto& [key, record] : record_map) {
    if (now - record.lastAlertAt <= policy.retention) {
      decision.updatedRecords.push_back(record);
    }
  }
  return decision;
}

}  // namespace rocket_watcher
