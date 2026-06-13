// Small ASCII string helpers shared across modules.
#pragma once

#include <string>
#include <vector>

namespace rocket_watcher {

std::string toLower(const std::string& text);
std::string trim(const std::string& text);
std::string join(const std::vector<std::string>& parts, const std::string& separator);
bool startsWith(const std::string& text, const std::string& prefix);

}  // namespace rocket_watcher
