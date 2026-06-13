#include "rocket_watcher/relevance.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "rocket_watcher/strings.hpp"

namespace rocket_watcher {

RelevanceResult evaluateRelevance(const Launch& launch, const std::string& launchProvider,
                                  const std::vector<std::string>& includeTerms) {
  if (includeTerms.empty()) {
    throw std::invalid_argument(
        "include_terms must not be empty; startup validation should reject this");
  }
  if (toLower(trim(launch.provider)) != toLower(trim(launchProvider))) {
    return RelevanceResult{
        false,
        {},
        "provider \"" + launch.provider + "\" does not match \"" + launchProvider + "\""};
  }

  std::vector<std::string> fields_lower;
  for (const auto& field : launch.searchableFields()) {
    fields_lower.push_back(toLower(field));
  }

  std::vector<std::string> matched;
  for (const auto& term : includeTerms) {
    std::string term_lower = toLower(term);
    bool found = std::ranges::any_of(fields_lower, [&](const std::string& f) {
      return f.find(term_lower) != std::string::npos;
    });
    if (found) matched.push_back(term);
  }
  if (matched.empty()) {
    return RelevanceResult{false, {}, "no include term matched any searchable field"};
  }
  return RelevanceResult{true, matched, "matched terms: " + join(matched, ", ")};
}

}  // namespace rocket_watcher
