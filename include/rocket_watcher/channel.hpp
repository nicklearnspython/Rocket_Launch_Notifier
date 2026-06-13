// Notification Channel adapter boundary.
//
// A channel receives formatted message content plus minimal metadata; it
// never sees full Launch or Alert objects and never decides wording.
#pragma once

#include <string>

#include "rocket_watcher/formatting.hpp"

namespace rocket_watcher {

struct DeliveryResult {
  std::string recipient;
  bool ok = false;
  std::string detail;
};

class NotificationChannel {
 public:
  virtual ~NotificationChannel() = default;

  // Attempts delivery to one configured recipient. Never throws.
  virtual DeliveryResult send(const Message& message, const std::string& recipientName) = 0;
};

}  // namespace rocket_watcher
