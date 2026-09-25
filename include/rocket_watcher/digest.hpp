// Weekly Digest: a once-a-week summary of upcoming Relevant Launches.
//
// The digest is for general awareness only. It is sent once per scheduled
// slot (e.g. Sunday 20:00 in the display timezone) and never corrected or
// followed up; countdown Alerts still cover the launches themselves.
//
// A slot is remembered once its digest is sent, so restarts do not repeat
// it. A slot missed entirely (e.g. the Watcher was down) is only caught up
// within kDigestGracePeriod; after that it is skipped until next week.
#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rocket_watcher/config.hpp"
#include "rocket_watcher/domain.hpp"

namespace rocket_watcher {

inline constexpr Duration kDigestGracePeriod = std::chrono::hours{3};

// The most recent scheduled slot at or before `now`.
TimePoint latestDigestSlot(TimePoint now, const DigestConfig& digest,
                           const std::chrono::time_zone* zone);

// The slot to send now, or nullopt when no digest is due: either the latest
// slot was already sent or it is older than kDigestGracePeriod.
std::optional<TimePoint> dueDigestSlot(TimePoint now, std::optional<TimePoint> lastSentSlot,
                                       const DigestConfig& digest,
                                       const std::chrono::time_zone* zone);

// Relevant, not-yet-ended Launches with a Launch Time in [now, now + lookahead),
// soonest first.
std::vector<Launch> selectDigestLaunches(const std::vector<Launch>& launches,
                                         const std::string& launchProvider,
                                         const std::vector<std::string>& includeTerms,
                                         TimePoint now, Duration lookahead);

// Remembers which digest slot was last sent.
class DigestStateStore {
 public:
  virtual ~DigestStateStore() = default;

  // Throws StorageError if existing state cannot be read.
  virtual std::optional<TimePoint> loadLastSentSlot() = 0;

  // Throws StorageError if state cannot be persisted.
  virtual void saveLastSentSlot(TimePoint slot) = 0;
};

class JsonDigestStateStore : public DigestStateStore {
 public:
  explicit JsonDigestStateStore(std::filesystem::path path);

  const std::filesystem::path& path() const { return path_; }

  std::optional<TimePoint> loadLastSentSlot() override;
  void saveLastSentSlot(TimePoint slot) override;

 private:
  std::filesystem::path path_;
};

}  // namespace rocket_watcher
