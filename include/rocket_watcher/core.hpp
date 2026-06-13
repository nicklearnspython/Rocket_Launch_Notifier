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

struct CycleDecision {
  std::vector<Alert> alerts;
  std::vector<AlertRecord> updatedRecords;
  std::vector<std::string> evaluations;
};

CycleDecision decideCycle(const std::vector<Launch>& launches,
                          const std::vector<AlertRecord>& records, const AlertPolicy& policy,
                          const std::string& launchProvider,
                          const std::vector<std::string>& includeTerms, TimePoint now);

}  // namespace rocket_watcher
