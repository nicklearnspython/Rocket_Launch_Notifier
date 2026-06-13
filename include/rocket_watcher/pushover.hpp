// Pushover Notification Channel adapter.
#pragma once

#include <chrono>
#include <map>
#include <string>

#include "rocket_watcher/channel.hpp"
#include "rocket_watcher/http.hpp"

namespace rocket_watcher {

inline constexpr const char* kPushoverApiUrl = "https://api.pushover.net/1/messages.json";

class PushoverChannel : public NotificationChannel {
 public:
  struct Options {
    std::string token;
    std::map<std::string, std::string> userKeys;  // recipient name -> user key
    std::string apiUrl = kPushoverApiUrl;
    std::chrono::seconds timeout = std::chrono::seconds{15};
    HttpPostForm httpPost;  // defaults to curlHttpPostForm()
  };

  explicit PushoverChannel(Options options);

  DeliveryResult send(const Message& message, const std::string& recipientName) override;

 private:
  static DeliveryResult interpretResponse(const std::string& recipientName,
                                          const HttpResponse& response);

  Options options_;
};

}  // namespace rocket_watcher
