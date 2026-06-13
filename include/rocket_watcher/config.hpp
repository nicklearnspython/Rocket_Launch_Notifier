// Behavior config (TOML) and secrets (.env / environment variables).
#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rocket_watcher/domain.hpp"

namespace rocket_watcher {

class ConfigError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct Recipient {
  std::string name;
  std::string userKeyEnv;
};

struct Config {
  std::string launchProvider;
  std::vector<std::string> includeTerms;
  std::vector<Recipient> recipients;
  std::string pushoverTokenEnv;
  int launchSoonMinutes = 60;
  int launchImminentMinutes = 10;
  int correctionTimeShiftMinutes = 30;
  int scheduleLookaheadHours = 30;
  int pollIntervalMinutes = 5;
  int retentionDays = 30;
  std::string displayTimezone = "America/Los_Angeles";
  std::string alertRecordsPath = "data/alert_records.json";
  std::string logPath = "logs/watcher.log";

  AlertPolicy policy() const;

  // Throws std::runtime_error for an unknown IANA timezone name.
  const std::chrono::time_zone* displayZone() const;
};

struct PushoverSecrets {
  std::string token;
  std::map<std::string, std::string> userKeys;  // recipient name -> user key
};

// Environment lookup, injectable for tests. Returns nullopt when unset.
using EnvLookup = std::function<std::optional<std::string>(const std::string&)>;

// Reads the process environment via std::getenv.
EnvLookup systemEnv();

// Loads and validates the behavior config. Throws ConfigError.
Config loadConfig(const std::filesystem::path& path);

// Reads Pushover secrets from the environment variables named in config.
// Throws ConfigError listing every missing variable.
PushoverSecrets resolveSecrets(const Config& config, const EnvLookup& env);

// Loads KEY=VALUE lines into the process environment without overriding
// existing values. Missing file is a no-op.
void loadDotenv(const std::filesystem::path& path);

}  // namespace rocket_watcher
