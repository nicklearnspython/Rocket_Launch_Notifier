// Launch Library 2 adapter.
//
// Translates LL2 responses into the Launch Schedule Source Contract. No
// LL2 payload fields or status strings leak past this module; unmapped
// values become project-level UNKNOWN plus a human-readable warning.
#pragma once

#include <chrono>
#include <functional>
#include <string>

#include "rocket_watcher/http.hpp"
#include "rocket_watcher/source.hpp"

namespace rocket_watcher {

inline constexpr const char* kLaunchLibrarySourceName = "ll2";
inline constexpr const char* kLaunchLibraryDefaultBaseUrl = "https://ll.thespacedevs.com/2.2.0";

class LaunchLibrarySource : public LaunchScheduleSource {
 public:
  struct Options {
    std::string baseUrl = kLaunchLibraryDefaultBaseUrl;
    int limit = 50;
    std::chrono::seconds timeout = std::chrono::seconds{30};
    HttpGet httpGet;  // defaults to curlHttpGet()
    std::function<TimePoint()> nowFn = utcNow;
  };

  LaunchLibrarySource();
  explicit LaunchLibrarySource(Options options);

  FetchResult fetch() override;

 private:
  Options options_;
};

}  // namespace rocket_watcher
