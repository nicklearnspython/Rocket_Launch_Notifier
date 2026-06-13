// Relevance matching: which Launches are Relevant Launches.
//
// A Relevant Launch requires a case-insensitive Launch Provider match and
// at least one case-insensitive Include Term match across Searchable Fields.
// Plain substring matching only; no regex, fuzzy, or tokenized matching.
#pragma once

#include <string>
#include <vector>

#include "rocket_watcher/domain.hpp"

namespace rocket_watcher {

struct RelevanceResult {
  bool relevant = false;
  std::vector<std::string> matchedTerms;
  std::string reason;
};

// Throws std::invalid_argument if includeTerms is empty; startup
// validation should have rejected that configuration already.
RelevanceResult evaluateRelevance(const Launch& launch, const std::string& launchProvider,
                                  const std::vector<std::string>& includeTerms);

}  // namespace rocket_watcher
