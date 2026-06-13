// Command-line entry points: once, watch, test-notification.
#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "rocket_watcher/channel.hpp"
#include "rocket_watcher/config.hpp"
#include "rocket_watcher/source.hpp"

namespace rocket_watcher {

// Factory seams so tests can substitute fake sources and channels.
using SourceFactory = std::function<std::unique_ptr<LaunchScheduleSource>(const Config&)>;
using ChannelFactory =
    std::function<std::unique_ptr<NotificationChannel>(const Config&, const PushoverSecrets&)>;

std::unique_ptr<LaunchScheduleSource> defaultBuildSource(const Config& config);
std::unique_ptr<NotificationChannel> defaultBuildChannel(const Config& config,
                                                         const PushoverSecrets& secrets);

struct CliDeps {
  SourceFactory buildSource = defaultBuildSource;
  ChannelFactory buildChannel = defaultBuildChannel;
  EnvLookup getEnv = systemEnv();
  std::ostream* out = &std::cout;
  std::ostream* err = &std::cerr;
  // Returns false to stop the watch loop; production sleeps and returns true.
  std::function<bool(std::chrono::seconds)> sleepFn;
};

// argv-style arguments without the program name. Returns the exit code:
// 0 success, 1 cycle/delivery failure, 2 config or usage error.
int runMain(const std::vector<std::string>& args, const CliDeps& deps = {});

}  // namespace rocket_watcher
