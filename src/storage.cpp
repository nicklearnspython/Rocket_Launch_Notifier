#include "rocket_watcher/storage.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "rocket_watcher/time_utils.hpp"

namespace rocket_watcher {

namespace {

using nlohmann::json;

json timeToJson(const std::optional<TimePoint>& time) {
  if (!time) return nullptr;
  return formatIso8601Utc(*time);
}

std::optional<TimePoint> optionalTimeFromJson(const json& value) {
  if (value.is_null()) return std::nullopt;
  auto parsed = parseIso8601(value.get<std::string>());
  if (!parsed) {
    throw StorageError("invalid timestamp in alert records: " + value.get<std::string>());
  }
  return parsed;
}

TimePoint timeFromJson(const json& value) {
  auto parsed = optionalTimeFromJson(value);
  if (!parsed) throw StorageError("missing required timestamp in alert records");
  return *parsed;
}

json recordToJson(const AlertRecord& record) {
  json sent_alerts = json::array();
  for (const auto& sent : record.sentAlerts) {
    sent_alerts.push_back({
        {"kind", toString(sent.kind)},
        {"created_at", timeToJson(sent.createdAt)},
        {"reason", sent.reason ? json(toString(*sent.reason)) : json(nullptr)},
    });
  }
  return {
      {"key", record.key},
      {"source_name", record.sourceName},
      {"source_launch_id", record.sourceLaunchId},
      {"launch_name", record.launchName},
      {"matched_terms", record.matchedTerms},
      {"last_facts",
       {
           {"launch_time", timeToJson(record.lastFacts.launchTime)},
           {"precision", toString(record.lastFacts.precision)},
           {"status", toString(record.lastFacts.status)},
       }},
      {"sent_alerts", sent_alerts},
      {"first_alert_at", timeToJson(record.firstAlertAt)},
      {"last_alert_at", timeToJson(record.lastAlertAt)},
  };
}

AlertRecord recordFromJson(const json& data) {
  const json& facts = data.at("last_facts");
  AlertRecord record;
  record.key = data.at("key").get<std::string>();
  record.sourceName = data.at("source_name").get<std::string>();
  record.sourceLaunchId = data.at("source_launch_id").get<std::string>();
  record.launchName = data.at("launch_name").get<std::string>();
  record.matchedTerms = data.at("matched_terms").get<std::vector<std::string>>();
  record.lastFacts = LaunchFacts{
      optionalTimeFromJson(facts.at("launch_time")),
      timingPrecisionFromString(facts.at("precision").get<std::string>()),
      launchStatusFromString(facts.at("status").get<std::string>()),
  };
  for (const auto& sent : data.at("sent_alerts")) {
    SentAlert sent_alert;
    sent_alert.kind = alertKindFromString(sent.at("kind").get<std::string>());
    sent_alert.createdAt = timeFromJson(sent.at("created_at"));
    if (sent.contains("reason") && !sent.at("reason").is_null()) {
      sent_alert.reason = correctionReasonFromString(sent.at("reason").get<std::string>());
    }
    record.sentAlerts.push_back(sent_alert);
  }
  record.firstAlertAt = timeFromJson(data.at("first_alert_at"));
  record.lastAlertAt = timeFromJson(data.at("last_alert_at"));
  return record;
}

}  // namespace

JsonAlertRecordStore::JsonAlertRecordStore(std::filesystem::path path) : path_(std::move(path)) {}

std::vector<AlertRecord> JsonAlertRecordStore::load() {
  if (!std::filesystem::exists(path_)) return {};
  try {
    std::ifstream file(path_);
    if (!file.is_open()) {
      throw StorageError("cannot open file");
    }
    json data = json::parse(file);
    std::vector<AlertRecord> records;
    for (const auto& entry : data.at("records")) {
      records.push_back(recordFromJson(entry));
    }
    return records;
  } catch (const std::exception& error) {
    throw StorageError("failed to load alert records from " + path_.string() + ": " + error.what());
  }
}

void JsonAlertRecordStore::save(const std::vector<AlertRecord>& records) {
  std::vector<AlertRecord> sorted = records;
  std::ranges::sort(sorted, {}, &AlertRecord::key);
  json payload = {{"version", kFileVersion}, {"records", json::array()}};
  for (const auto& record : sorted) {
    payload["records"].push_back(recordToJson(record));
  }
  std::string text = payload.dump(2);

  std::filesystem::path tmp_path = path_;
  tmp_path += ".tmp";
  try {
    if (path_.has_parent_path()) {
      std::filesystem::create_directories(path_.parent_path());
    }
    {
      std::ofstream file(tmp_path, std::ios::trunc);
      if (!file.is_open()) {
        throw StorageError("cannot open temporary file");
      }
      file << text;
      file.close();
      if (file.fail()) {
        throw StorageError("write to temporary file failed");
      }
    }
    replaceFile(tmp_path, path_);
  } catch (const std::exception& error) {
    std::error_code ignored;
    std::filesystem::remove(tmp_path, ignored);
    throw StorageError("failed to save alert records to " + path_.string() + ": " + error.what());
  }
}

void JsonAlertRecordStore::replaceFile(const std::filesystem::path& temporary,
                                       const std::filesystem::path& destination) {
  std::filesystem::rename(temporary, destination);
}

}  // namespace rocket_watcher
