#include "rocket_watcher/digest.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "rocket_watcher/relevance.hpp"
#include "rocket_watcher/storage.hpp"
#include "rocket_watcher/time_utils.hpp"

namespace rocket_watcher {

using nlohmann::json;

TimePoint latestDigestSlot(TimePoint now, const DigestConfig& digest,
                           const std::chrono::time_zone* zone) {
  using std::chrono::days;
  std::chrono::local_seconds local_now = zone->to_local(now);
  std::chrono::local_days today = std::chrono::floor<days>(local_now);
  days since_digest_day = std::chrono::weekday{today} - digest.day;
  std::chrono::local_seconds slot = today - since_digest_day + digest.timeOfDay;
  if (slot > local_now) slot -= days{7};
  // A slot inside a DST gap or overlap resolves to its earliest instant.
  return zone->to_sys(slot, std::chrono::choose::earliest);
}

std::optional<TimePoint> dueDigestSlot(TimePoint now, std::optional<TimePoint> lastSentSlot,
                                       const DigestConfig& digest,
                                       const std::chrono::time_zone* zone) {
  TimePoint slot = latestDigestSlot(now, digest, zone);
  if (lastSentSlot && *lastSentSlot >= slot) return std::nullopt;
  if (now - slot >= kDigestGracePeriod) return std::nullopt;
  return slot;
}

std::vector<Launch> selectDigestLaunches(const std::vector<Launch>& launches,
                                         const std::string& launchProvider,
                                         const std::vector<std::string>& includeTerms,
                                         TimePoint now, Duration lookahead) {
  std::vector<Launch> selected;
  for (const auto& launch : launches) {
    if (!launch.launchTime || *launch.launchTime < now || *launch.launchTime >= now + lookahead) {
      continue;
    }
    if (launch.status == LaunchStatus::kEnded) continue;
    if (!evaluateRelevance(launch, launchProvider, includeTerms).relevant) continue;
    selected.push_back(launch);
  }
  std::ranges::stable_sort(selected, {}, [](const Launch& launch) { return *launch.launchTime; });
  return selected;
}

JsonDigestStateStore::JsonDigestStateStore(std::filesystem::path path) : path_(std::move(path)) {}

std::optional<TimePoint> JsonDigestStateStore::loadLastSentSlot() {
  if (!std::filesystem::exists(path_)) return std::nullopt;
  try {
    std::ifstream file(path_);
    if (!file.is_open()) throw StorageError("cannot open file");
    json data = json::parse(file);
    auto slot = parseIso8601(data.at("last_sent_slot").get<std::string>());
    if (!slot) throw StorageError("invalid last_sent_slot timestamp");
    return slot;
  } catch (const std::exception& error) {
    throw StorageError("failed to load digest state from " + path_.string() + ": " + error.what());
  }
}

void JsonDigestStateStore::saveLastSentSlot(TimePoint slot) {
  json payload = {{"version", 1}, {"last_sent_slot", formatIso8601Utc(slot)}};
  std::filesystem::path tmp_path = path_;
  tmp_path += ".tmp";
  try {
    if (path_.has_parent_path()) {
      std::filesystem::create_directories(path_.parent_path());
    }
    {
      std::ofstream file(tmp_path, std::ios::trunc);
      if (!file.is_open()) throw StorageError("cannot open temporary file");
      file << payload.dump(2);
      file.close();
      if (file.fail()) throw StorageError("write to temporary file failed");
    }
    std::filesystem::rename(tmp_path, path_);
  } catch (const std::exception& error) {
    std::error_code ignored;
    std::filesystem::remove(tmp_path, ignored);
    throw StorageError("failed to save digest state to " + path_.string() + ": " + error.what());
  }
}

}  // namespace rocket_watcher
