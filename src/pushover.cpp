#include "rocket_watcher/pushover.hpp"

#include <utility>

#include <nlohmann/json.hpp>

namespace rocket_watcher {

using nlohmann::json;

PushoverChannel::PushoverChannel(Options options) : options_(std::move(options)) {
  if (!options_.httpPost) options_.httpPost = curlHttpPostForm();
}

DeliveryResult PushoverChannel::send(const Message& message, const std::string& recipientName) {
  auto user_key = options_.userKeys.find(recipientName);
  if (user_key == options_.userKeys.end() || user_key->second.empty()) {
    return DeliveryResult{recipientName, false, "no Pushover user key resolved for this recipient"};
  }
  std::vector<std::pair<std::string, std::string>> fields = {
      {"token", options_.token},
      {"user", user_key->second},
      {"title", message.title},
      {"message", message.body},
  };
  HttpResponse response;
  try {
    response = options_.httpPost(options_.apiUrl, fields, options_.timeout);
  } catch (const std::exception& error) {
    return DeliveryResult{recipientName, false, std::string("request failed: ") + error.what()};
  }
  return interpretResponse(recipientName, response);
}

DeliveryResult PushoverChannel::interpretResponse(const std::string& recipientName,
                                                  const HttpResponse& response) {
  json parsed = json::parse(response.body, nullptr, /*allow_exceptions=*/false);
  if (!parsed.is_object()) parsed = json::object();

  long api_status = parsed.value("status", -1);
  if (response.status == 200 && api_status == 1) {
    return DeliveryResult{recipientName, true, "delivered"};
  }

  std::string detail = "HTTP " + std::to_string(response.status) + ", api status " +
                       (parsed.contains("status") ? std::to_string(api_status) : "unknown");
  if (parsed.contains("errors") && parsed.at("errors").is_array()) {
    std::string joined;
    for (const auto& entry : parsed.at("errors")) {
      if (!joined.empty()) joined += "; ";
      joined += entry.is_string() ? entry.get<std::string>() : entry.dump();
    }
    if (!joined.empty()) detail += ": " + joined;
  }
  return DeliveryResult{recipientName, false, detail};
}

}  // namespace rocket_watcher
