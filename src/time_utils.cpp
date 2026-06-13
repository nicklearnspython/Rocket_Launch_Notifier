#include "rocket_watcher/time_utils.hpp"

#include <array>
#include <cstdio>

namespace rocket_watcher {

namespace {

struct CivilTime {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  long hour = 0;
  long minute = 0;
  long second = 0;
};

CivilTime splitLocal(std::chrono::local_seconds local) {
  auto day_point = std::chrono::floor<std::chrono::days>(local);
  std::chrono::year_month_day ymd{day_point};
  std::chrono::hh_mm_ss time_of_day{local - day_point};
  return CivilTime{
      static_cast<int>(ymd.year()),     static_cast<unsigned>(ymd.month()),
      static_cast<unsigned>(ymd.day()), time_of_day.hours().count(),
      time_of_day.minutes().count(),    static_cast<long>(time_of_day.seconds().count()),
  };
}

CivilTime splitUtc(TimePoint time) {
  return splitLocal(std::chrono::local_seconds{time.time_since_epoch()});
}

std::string localAbbrev(TimePoint time, const std::chrono::time_zone* zone) {
  return zone->get_info(time).abbrev;
}

CivilTime splitInZone(TimePoint time, const std::chrono::time_zone* zone) {
  return splitLocal(zone->to_local(time));
}

// Parses a +HH:MM / -HH:MM (or +HHMM / -HHMM) suffix starting at `rest`.
// Advances `rest` past the offset; returns nullopt if it is malformed.
std::optional<std::chrono::seconds> parseUtcOffset(const char*& rest) {
  int sign = (*rest == '-') ? -1 : 1;
  ++rest;
  int off_hour = 0;
  int off_minute = 0;
  int off_consumed = 0;
  bool with_colon = std::sscanf(rest, "%2d:%2d%n", &off_hour, &off_minute, &off_consumed) == 2 &&
                    off_consumed == 5;
  if (!with_colon) {
    bool without_colon =
        std::sscanf(rest, "%2d%2d%n", &off_hour, &off_minute, &off_consumed) == 2 &&
        off_consumed == 4;
    if (!without_colon) return std::nullopt;
  }
  rest += off_consumed;
  return sign * (std::chrono::hours{off_hour} + std::chrono::minutes{off_minute});
}

}  // namespace

std::string formatIso8601Utc(TimePoint time) {
  CivilTime c = splitUtc(time);
  std::array<char, 40> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%04d-%02u-%02uT%02ld:%02ld:%02ld+00:00", c.year,
                c.month, c.day, c.hour, c.minute, c.second);
  return buffer.data();
}

std::optional<TimePoint> parseIso8601(const std::string& text) {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int consumed = 0;
  if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day, &hour, &minute,
                  &second, &consumed) != 6) {
    return std::nullopt;
  }
  const char* rest = text.c_str() + consumed;

  // Truncate fractional seconds.
  if (*rest == '.') {
    ++rest;
    if (*rest < '0' || *rest > '9') return std::nullopt;
    while (*rest >= '0' && *rest <= '9') ++rest;
  }

  // Offset: Z, +HH:MM/-HH:MM (or +HHMM), or absent (treated as UTC).
  std::chrono::seconds offset{0};
  if (*rest == 'Z' || *rest == 'z') {
    ++rest;
  } else if (*rest == '+' || *rest == '-') {
    std::optional<std::chrono::seconds> parsed_offset = parseUtcOffset(rest);
    if (!parsed_offset) return std::nullopt;
    offset = *parsed_offset;
  }
  if (*rest != '\0') return std::nullopt;

  std::chrono::year_month_day ymd{std::chrono::year{year},
                                  std::chrono::month{static_cast<unsigned>(month)},
                                  std::chrono::day{static_cast<unsigned>(day)}};
  if (!ymd.ok() || hour > 23 || minute > 59 || second > 60) return std::nullopt;
  auto wall = std::chrono::sys_days{ymd} + std::chrono::hours{hour} + std::chrono::minutes{minute} +
              std::chrono::seconds{second};
  return TimePoint{wall - offset};
}

const std::chrono::time_zone* locateZone(const std::string& name) {
  return std::chrono::locate_zone(name);
}

std::string formatDisplay(std::optional<TimePoint> time, const std::chrono::time_zone* zone) {
  if (!time) return "unknown time";
  CivilTime c = splitInZone(*time, zone);
  std::array<char, 32> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%04d-%02u-%02u %02ld:%02ld ", c.year, c.month, c.day,
                c.hour, c.minute);
  return buffer.data() + localAbbrev(*time, zone);
}

std::string formatDisplayHour(TimePoint time, const std::chrono::time_zone* zone) {
  CivilTime c = splitInZone(time, zone);
  std::array<char, 48> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%04d-%02u-%02u around %02ld:00 ", c.year, c.month,
                c.day, c.hour);
  return buffer.data() + localAbbrev(time, zone);
}

std::string formatUtcSeconds(TimePoint time) {
  CivilTime c = splitUtc(time);
  std::array<char, 32> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%04d-%02u-%02u %02ld:%02ld:%02ld", c.year, c.month,
                c.day, c.hour, c.minute, c.second);
  return buffer.data();
}

std::string formatDisplayShort(TimePoint time, const std::chrono::time_zone* zone) {
  CivilTime c = splitInZone(time, zone);
  std::array<char, 16> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%02ld:%02ld ", c.hour, c.minute);
  return buffer.data() + localAbbrev(time, zone);
}

}  // namespace rocket_watcher
