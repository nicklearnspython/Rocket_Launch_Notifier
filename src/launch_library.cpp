#include "rocket_watcher/launch_library.hpp"

#include <array>
#include <map>
#include <optional>

#include <nlohmann/json.hpp>

#include "rocket_watcher/strings.hpp"
#include "rocket_watcher/time_utils.hpp"

namespace rocket_watcher {

namespace {

using nlohmann::json;

// LL2 net_precision names -> project Timing Precision.
const std::map<std::string, TimingPrecision> kPrecisionMap = {
    {"second", TimingPrecision::kPrecise},    {"minute", TimingPrecision::kPrecise},
    {"hour", TimingPrecision::kHour},         {"day", TimingPrecision::kImprecise},
    {"week", TimingPrecision::kImprecise},    {"month", TimingPrecision::kImprecise},
    {"quarter", TimingPrecision::kImprecise}, {"year", TimingPrecision::kImprecise},
    {"decade", TimingPrecision::kImprecise},
};

// LL2 also uses calendar-window names like "Quarter 2", "Year Half 1", or
// "Fiscal Year 2027"; anything in these families is imprecise.
constexpr std::array<const char*, 7> kImprecisePrefixes = {"day",  "week",   "month", "quarter",
                                                           "year", "decade", "fiscal"};

// LL2 status abbreviations -> project Launch Status.
//
// "TBC"/"TBD" (not yet confirmed/determined) mean the schedule slot is
// speculative, so the conservative project-level reading is unknown (silent).
const std::map<std::string, LaunchStatus> kStatusMap = {
    {"go", LaunchStatus::kGo},
    {"hold", LaunchStatus::kNoGo},
    {"success", LaunchStatus::kEnded},
    {"failure", LaunchStatus::kEnded},
    {"partial failure", LaunchStatus::kEnded},
    {"in flight", LaunchStatus::kEnded},
    {"tbc", LaunchStatus::kUnknown},
    {"tbd", LaunchStatus::kUnknown},
};

std::optional<std::string> optionalStringField(const json& object, const std::string& key) {
  if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) {
    return std::nullopt;
  }
  std::string value = object.at(key).get<std::string>();
  if (value.empty()) return std::nullopt;
  return value;
}

std::optional<TimePoint> parseNet(const json& net, const std::string& label,
                                  std::vector<std::string>& warnings) {
  if (net.is_null() || (net.is_string() && net.get<std::string>().empty())) {
    return std::nullopt;
  }
  std::string text = net.is_string() ? net.get<std::string>() : net.dump();
  auto parsed = parseIso8601(text);
  if (!parsed) {
    warnings.push_back(label + ": unparseable net time \"" + text + "\"");
    return std::nullopt;
  }
  return parsed;
}

TimingPrecision mapPrecision(const json& netPrecision, const std::string& label,
                             std::vector<std::string>& warnings) {
  auto name = optionalStringField(netPrecision, "name");
  if (!name) {
    warnings.push_back(label + ": missing net precision; treating timing as unknown");
    return TimingPrecision::kUnknown;
  }
  std::string normalized = toLower(trim(*name));
  auto found = kPrecisionMap.find(normalized);
  if (found != kPrecisionMap.end()) return found->second;
  for (const char* prefix : kImprecisePrefixes) {
    if (startsWith(normalized, prefix)) return TimingPrecision::kImprecise;
  }
  warnings.push_back(label + ": unmapped net precision \"" + *name +
                     "\"; treating timing as unknown");
  return TimingPrecision::kUnknown;
}

LaunchStatus mapStatus(const json& status, const std::string& label,
                       std::vector<std::string>& warnings) {
  auto abbrev = optionalStringField(status, "abbrev");
  if (!abbrev) {
    warnings.push_back(label + ": missing status; treating status as unknown");
    return LaunchStatus::kUnknown;
  }
  auto found = kStatusMap.find(toLower(trim(*abbrev)));
  if (found == kStatusMap.end()) {
    warnings.push_back(label + ": unmapped status \"" + *abbrev + "\"; treating status as unknown");
    return LaunchStatus::kUnknown;
  }
  return found->second;
}

Launch mapLaunch(const json& item, std::vector<std::string>& warnings) {
  const json& id = item.at("id");
  std::string launch_id = id.is_string() ? id.get<std::string>() : id.dump();
  std::string name = optionalStringField(item, "name").value_or("launch " + launch_id);
  std::string label = name + " [" + launch_id + "]";

  std::string provider;
  if (item.contains("launch_service_provider")) {
    provider = optionalStringField(item.at("launch_service_provider"), "name").value_or("");
  }
  if (provider.empty()) {
    warnings.push_back(label + ": missing launch service provider name");
  }

  auto launch_time = parseNet(item.value("net", json(nullptr)), label, warnings);
  TimingPrecision precision =
      mapPrecision(item.value("net_precision", json(nullptr)), label, warnings);
  if (!launch_time && precision != TimingPrecision::kUnknown) {
    precision = TimingPrecision::kUnknown;
  }
  LaunchStatus status = mapStatus(item.value("status", json(nullptr)), label, warnings);

  json mission = item.value("mission", json(nullptr));
  json pad = item.value("pad", json(nullptr));
  auto pad_name = optionalStringField(pad, "name");
  std::optional<std::string> location_name;
  if (pad.is_object() && pad.contains("location")) {
    location_name = optionalStringField(pad.at("location"), "name");
  }
  if (pad_name && location_name) {
    pad_name = *pad_name + ", " + *location_name;
  } else if (!pad_name) {
    pad_name = location_name;
  }

  Launch launch;
  launch.sourceName = kLaunchLibrarySourceName;
  launch.sourceLaunchId = launch_id;
  launch.name = name;
  launch.provider = provider;
  launch.launchTime = launch_time;
  launch.precision = precision;
  launch.status = status;
  launch.missionName = optionalStringField(mission, "name");
  launch.missionDescription = optionalStringField(mission, "description");
  launch.padName = pad_name;
  return launch;
}

}  // namespace

LaunchLibrarySource::LaunchLibrarySource() : LaunchLibrarySource(Options{}) {}

LaunchLibrarySource::LaunchLibrarySource(Options options) : options_(std::move(options)) {
  if (!options_.httpGet) options_.httpGet = curlHttpGet();
  while (!options_.baseUrl.empty() && options_.baseUrl.back() == '/') {
    options_.baseUrl.pop_back();
  }
}

FetchResult LaunchLibrarySource::fetch() {
  std::string url = options_.baseUrl + "/launch/upcoming/?limit=" + std::to_string(options_.limit) +
                    "&hide_recent_previous=true";
  json payload;
  try {
    payload = json::parse(options_.httpGet(url, options_.timeout));
  } catch (const std::exception& error) {
    throw SourceError(std::string("Launch Library 2 fetch failed: ") + error.what());
  }

  FetchResult result;
  result.fetchedAt = options_.nowFn();
  for (const auto& item : payload.value("results", json::array())) {
    try {
      result.launches.push_back(mapLaunch(item, result.warnings));
    } catch (const std::exception& error) {
      result.warnings.push_back(std::string("skipped unmappable launch payload: ") + error.what());
    }
  }
  return result;
}

}  // namespace rocket_watcher
