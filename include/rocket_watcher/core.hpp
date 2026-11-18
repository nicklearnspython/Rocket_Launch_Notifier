// The pure decision core.
//
// Consumes normalized Launches, existing Alert Records, alert policy, and
// the current time; returns semantic Alerts plus the new updated Alert
// Records collection. No network, filesystem, or channel dependencies.
#pragma once

#include <string>
#include <vector>

#include "rocket_watcher/domain.hpp"

namespace rocket_watcher {

// Why one launch did or did not produce an Alert this cycle.
struct Evaluation {
  std::string launchKey;
  std::string launchName;
  bool relevant = false;
  bool alerted = false;
  std::string note;  // e.g. "silent (status unknown)"

  // "Falcon 9 Block 5 | Starlink Group 17-44 [ll2:...]: silent (status unknown)"
  std::string line() const { return launchName + " [" + launchKey + "]: " + note; }
};

struct CycleDecision {
  std::vector<Alert> alerts;
  std::vector<AlertRecord> updatedRecords;
  std::vector<Evaluation> evaluations;
};

CycleDecision decideCycle(const std::vector<Launch>& launches,
                          const std::vector<AlertRecord>& records, const AlertPolicy& policy,
                          const std::string& launchProvider,
                          const std::vector<std::string>& includeTerms, TimePoint now);

}  // namespace rocket_watcher
