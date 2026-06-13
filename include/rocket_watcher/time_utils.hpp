// UTC timestamp parsing/serialization and display-timezone rendering.
#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "rocket_watcher/domain.hpp"

namespace rocket_watcher {

// Serializes as "YYYY-MM-DDTHH:MM:SS+00:00" (matches Python's isoformat()).
std::string formatIso8601Utc(TimePoint time);

// Parses ISO-8601 timestamps like "2026-06-09T18:45:00Z",
// "2026-06-09T18:45:00+00:00", or "2026-06-09T18:45:00.123-07:00".
// Fractional seconds are truncated; a missing offset means UTC.
// Returns nullopt if the text is not a valid timestamp.
std::optional<TimePoint> parseIso8601(const std::string& text);

// Looks up an IANA timezone. Throws std::runtime_error for unknown names.
const std::chrono::time_zone* locateZone(const std::string& name);

// "2026-06-09 11:45 PDT", or "unknown time" when absent.
std::string formatDisplay(std::optional<TimePoint> time, const std::chrono::time_zone* zone);

// "2026-06-09 around 11:00 PDT" for hour-precision wording.
std::string formatDisplayHour(TimePoint time, const std::chrono::time_zone* zone);

// "2026-06-09 18:00:00" (UTC wall clock, for log lines).
std::string formatUtcSeconds(TimePoint time);

// "11:45 PDT" (display-timezone wall clock, for log lines).
std::string formatDisplayShort(TimePoint time, const std::chrono::time_zone* zone);

}  // namespace rocket_watcher
