#include "rocket_watcher/strings.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>

namespace rocket_watcher {

std::string toLower(const std::string& text) {
  std::string result = text;
  std::ranges::transform(result, result.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

std::string trim(const std::string& text) {
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  auto begin = std::ranges::find_if_not(text, is_space);
  auto end = std::ranges::find_if_not(text | std::views::reverse, is_space).base();
  return (begin < end) ? std::string(begin, end) : std::string();
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
  std::string result;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) result += separator;
    result += parts[i];
  }
  return result;
}

bool startsWith(const std::string& text, const std::string& prefix) {
  return text.starts_with(prefix);
}

}  // namespace rocket_watcher
