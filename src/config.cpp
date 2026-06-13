#include "rocket_watcher/config.hpp"

#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include <toml++/toml.hpp>

#include "rocket_watcher/strings.hpp"
#include "rocket_watcher/time_utils.hpp"

namespace rocket_watcher {

namespace {

std::string requireString(const toml::table* table, const std::string& key,
                          const std::string& where) {
  const toml::node* node = (table != nullptr) ? table->get(key) : nullptr;
  const auto* value = (node != nullptr) ? node->as_string() : nullptr;
  if (value == nullptr || trim(value->get()).empty()) {
    throw ConfigError(where + "." + key + " must be a non-empty string");
  }
  return trim(value->get());
}

int optionalPositiveInt(const toml::table* table, const std::string& key, const std::string& where,
                        int defaultValue) {
  const toml::node* node = (table != nullptr) ? table->get(key) : nullptr;
  if (node == nullptr) return defaultValue;
  const auto* value = node->as_integer();
  if (value == nullptr || value->get() <= 0) {
    throw ConfigError(where + "." + key + " must be a positive integer");
  }
  return static_cast<int>(value->get());
}

std::string optionalString(const toml::table* table, const std::string& key,
                           const std::string& defaultValue) {
  const toml::node* node = (table != nullptr) ? table->get(key) : nullptr;
  const auto* value = (node != nullptr) ? node->as_string() : nullptr;
  return (value != nullptr) ? value->get() : defaultValue;
}

}  // namespace

AlertPolicy Config::policy() const {
  return AlertPolicy{
      std::chrono::minutes{launchSoonMinutes},
      std::chrono::minutes{launchImminentMinutes},
      std::chrono::minutes{correctionTimeShiftMinutes},
      std::chrono::hours{scheduleLookaheadHours},
      std::chrono::days{retentionDays},
  };
}

const std::chrono::time_zone* Config::displayZone() const { return locateZone(displayTimezone); }

EnvLookup systemEnv() {
  return [](const std::string& name) -> std::optional<std::string> {
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) return std::nullopt;
    return std::string(value);
  };
}

Config loadConfig(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    throw ConfigError("config file not found: " + path.string());
  }
  toml::table data;
  try {
    data = toml::parse_file(path.string());
  } catch (const toml::parse_error& error) {
    std::ostringstream message;
    message << "invalid TOML in " << path.string() << ": " << error.description();
    throw ConfigError(message.str());
  }

  const toml::table* relevance = data["relevance"].as_table();
  std::string launch_provider = requireString(relevance, "launch_provider", "relevance");

  const toml::array* raw_terms =
      (relevance != nullptr) ? relevance->get_as<toml::array>("include_terms") : nullptr;
  if (raw_terms == nullptr || raw_terms->empty()) {
    throw ConfigError(
        "relevance.include_terms must be a non-empty list; "
        "an implicit all-provider mode is not supported");
  }
  std::vector<std::string> include_terms;
  for (const auto& node : *raw_terms) {
    const auto* term = node.as_string();
    if (term == nullptr || trim(term->get()).empty()) {
      throw ConfigError("relevance.include_terms entries must be non-empty strings");
    }
    include_terms.push_back(trim(term->get()));
  }

  const toml::array* raw_recipients = data.get_as<toml::array>("recipients");
  if (raw_recipients == nullptr || raw_recipients->empty()) {
    throw ConfigError("at least one [[recipients]] entry is required");
  }
  std::vector<Recipient> recipients;
  std::set<std::string> names;
  for (size_t i = 0; i < raw_recipients->size(); ++i) {
    std::string where = "recipients[" + std::to_string(i) + "]";
    const toml::table* entry = raw_recipients->get(i)->as_table();
    Recipient recipient{requireString(entry, "name", where),
                        requireString(entry, "user_key_env", where)};
    if (!names.insert(recipient.name).second) {
      throw ConfigError("recipient names must be unique");
    }
    recipients.push_back(recipient);
  }

  const toml::table* pushover = data["pushover"].as_table();
  std::string token_env = requireString(pushover, "token_env", "pushover");

  const toml::table* alerts = data["alerts"].as_table();
  const toml::table* watcher = data["watcher"].as_table();

  Config config;
  config.launchProvider = launch_provider;
  config.includeTerms = include_terms;
  config.recipients = recipients;
  config.pushoverTokenEnv = token_env;
  config.launchSoonMinutes = optionalPositiveInt(alerts, "launch_soon_minutes", "alerts", 60);
  config.launchImminentMinutes =
      optionalPositiveInt(alerts, "launch_imminent_minutes", "alerts", 10);
  config.correctionTimeShiftMinutes =
      optionalPositiveInt(alerts, "correction_time_shift_minutes", "alerts", 30);
  config.scheduleLookaheadHours =
      optionalPositiveInt(alerts, "schedule_lookahead_hours", "alerts", 30);
  config.pollIntervalMinutes = optionalPositiveInt(watcher, "poll_interval_minutes", "watcher", 5);
  config.retentionDays = optionalPositiveInt(watcher, "retention_days", "watcher", 30);
  config.displayTimezone = optionalString(watcher, "display_timezone", "America/Los_Angeles");
  config.alertRecordsPath =
      optionalString(watcher, "alert_records_path", "data/alert_records.json");
  config.logPath = optionalString(watcher, "log_path", "logs/watcher.log");

  if (config.launchImminentMinutes >= config.launchSoonMinutes) {
    throw ConfigError("alerts.launch_imminent_minutes must be less than launch_soon_minutes");
  }
  try {
    config.displayZone();
  } catch (const std::exception&) {
    throw ConfigError("watcher.display_timezone is not a valid IANA timezone: \"" +
                      config.displayTimezone + "\"");
  }
  return config;
}

PushoverSecrets resolveSecrets(const Config& config, const EnvLookup& env) {
  std::vector<std::string> missing;
  PushoverSecrets secrets;

  std::string token = trim(env(config.pushoverTokenEnv).value_or(""));
  if (token.empty()) {
    missing.push_back(config.pushoverTokenEnv);
  }
  secrets.token = token;

  for (const auto& recipient : config.recipients) {
    std::string key = trim(env(recipient.userKeyEnv).value_or(""));
    if (key.empty()) {
      missing.push_back(recipient.userKeyEnv);
    } else {
      secrets.userKeys[recipient.name] = key;
    }
  }
  if (!missing.empty()) {
    throw ConfigError("missing required secret environment variables: " + join(missing, ", "));
  }
  return secrets;
}

namespace {

void setEnvIfUnset(const std::string& key, const std::string& value) {
#ifdef _WIN32
  if (std::getenv(key.c_str()) == nullptr) {
    _putenv_s(key.c_str(), value.c_str());
  }
#else
  ::setenv(key.c_str(), value.c_str(), /*overwrite=*/0);
#endif
}

std::string stripQuotes(const std::string& text) {
  std::string result = text;
  while (!result.empty() && (result.front() == '\'' || result.front() == '"')) {
    result.erase(result.begin());
  }
  while (!result.empty() && (result.back() == '\'' || result.back() == '"')) {
    result.pop_back();
  }
  return result;
}

}  // namespace

void loadDotenv(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file.is_open()) return;
  std::string raw_line;
  while (std::getline(file, raw_line)) {
    std::string line = trim(raw_line);
    if (line.empty() || line.front() == '#') continue;
    size_t equals = line.find('=');
    if (equals == std::string::npos) continue;
    std::string key = trim(line.substr(0, equals));
    std::string value = stripQuotes(trim(line.substr(equals + 1)));
    if (!key.empty()) setEnvIfUnset(key, value);
  }
}

}  // namespace rocket_watcher
