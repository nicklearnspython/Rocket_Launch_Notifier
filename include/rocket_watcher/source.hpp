// Launch Schedule Source adapter boundary.
//
// A source returns a minimal fetch result: normalized Launches, the fetch
// time, and plain-string warnings for orchestration/logging. The decision
// core receives only the Launches.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "rocket_watcher/domain.hpp"

namespace rocket_watcher {

class SourceError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct FetchResult {
  std::vector<Launch> launches;
  TimePoint fetchedAt;
  std::vector<std::string> warnings;
};

class LaunchScheduleSource {
 public:
  virtual ~LaunchScheduleSource() = default;

  // Fetches upcoming launches. Throws SourceError on failure.
  virtual FetchResult fetch() = 0;
};

}  // namespace rocket_watcher
