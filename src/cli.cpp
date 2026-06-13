#include "rocket_watcher/cli.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <thread>

#include "rocket_watcher/launch_library.hpp"
#include "rocket_watcher/orchestration.hpp"
#include "rocket_watcher/pushover.hpp"
#include "rocket_watcher/time_utils.hpp"

namespace rocket_watcher {

namespace {

std::atomic<bool> g_stop_requested{false};

void handleStopSignal(int) { g_stop_requested = true; }

// Sleeps in one-second slices so SIGINT/SIGTERM stop the loop promptly.
// Returns false once a stop was requested.
bool interruptibleSleep(std::chrono::seconds duration) {
  auto remaining = duration;
  while (remaining > std::chrono::seconds::zero()) {
    if (g_stop_requested) return false;
    auto slice = std::min(remaining, std::chrono::seconds{1});
    std::this_thread::sleep_for(slice);
    remaining -= slice;
  }
  return !g_stop_requested;
}

// Used in dry runs, where no Notifications may be sent.
class NullChannel : public NotificationChannel {
 public:
  DeliveryResult send(const Message&, const std::string& recipientName) override {
    return DeliveryResult{recipientName, true, "dry run, not sent"};
  }
};

Logger makeLogger(const Config& config, bool toFile, std::ostream& out) {
  const std::chrono::time_zone* zone = config.displayZone();
  std::filesystem::path log_path = config.logPath;
  return [zone, log_path, toFile, &out](const std::string& message) {
    TimePoint now = utcNow();
    std::string line =
        "[" + formatUtcSeconds(now) + " UTC / " + formatDisplayShort(now, zone) + "] " + message;
    out << line << "\n";
    out.flush();
    if (toFile) {
      if (log_path.has_parent_path()) {
        std::filesystem::create_directories(log_path.parent_path());
      }
      std::ofstream handle(log_path, std::ios::app);
      handle << line << "\n";
    }
  };
}

int commandOnce(const Config& config, bool dryRun, const CliDeps& deps) {
  std::unique_ptr<NotificationChannel> channel;
  if (dryRun) {
    channel = std::make_unique<NullChannel>();
  } else {
    PushoverSecrets secrets = resolveSecrets(config, deps.getEnv);
    channel = deps.buildChannel(config, secrets);
  }
  Logger log = makeLogger(config, /*toFile=*/!dryRun, *deps.out);
  auto source = deps.buildSource(config);
  JsonAlertRecordStore store(config.alertRecordsPath);
  CycleOptions options;
  options.dryRun = dryRun;
  CycleOutcome outcome = runCycle(*source, store, *channel, config, log, options);
  return outcome.ok() ? 0 : 1;
}

int commandWatch(const Config& config, const CliDeps& deps) {
  PushoverSecrets secrets = resolveSecrets(config, deps.getEnv);
  auto channel = deps.buildChannel(config, secrets);
  auto source = deps.buildSource(config);
  JsonAlertRecordStore store(config.alertRecordsPath);
  Logger log = makeLogger(config, /*toFile=*/true, *deps.out);

  std::signal(SIGINT, handleStopSignal);
  std::signal(SIGTERM, handleStopSignal);
  auto sleep_fn = deps.sleepFn ? deps.sleepFn : interruptibleSleep;
  auto interval = std::chrono::minutes{config.pollIntervalMinutes};

  log("watch started; polling every " + std::to_string(config.pollIntervalMinutes) + " minute(s)");
  while (true) {
    CycleOutcome outcome = runCycle(*source, store, *channel, config, log);
    if (!outcome.ok()) {
      log("cycle finished with failures; continuing to poll");
    }
    if (!sleep_fn(interval)) break;
  }
  log("watch stopped");
  return 0;
}

int commandTestNotification(const Config& config, const CliDeps& deps) {
  PushoverSecrets secrets = resolveSecrets(config, deps.getEnv);
  auto channel = deps.buildChannel(config, secrets);
  TimePoint now = utcNow();
  Message message{
      "Rocket Watcher test notification",
      "Test notification sent at " + formatUtcSeconds(now) + " UTC (" +
          formatDisplay(now, config.displayZone()) + "). Pushover delivery is working.",
      "test",
      "test:synthetic",
      "test notification",
  };
  bool any_failed = false;
  for (const auto& recipient : config.recipients) {
    DeliveryResult result = channel->send(message, recipient.name);
    std::string status = result.ok ? "delivered" : "FAILED";
    *deps.out << status << " to " << recipient.name << ": " << result.detail << "\n";
    if (!result.ok) any_failed = true;
  }
  return any_failed ? 1 : 0;
}

void printUsage(std::ostream& out) {
  out << "usage: rocket-watcher [--config PATH] [--env-file PATH] COMMAND\n"
         "\n"
         "Personal SpaceX launch watcher.\n"
         "\n"
         "commands:\n"
         "  once [--dry-run]    run a single Watcher cycle; --dry-run evaluates\n"
         "                      launches and prints candidate alerts without\n"
         "                      sending, writing records, or writing logs\n"
         "  watch               run the long-lived polling loop\n"
         "  test-notification   send a synthetic notification to every recipient,\n"
         "                      bypassing launch evaluation\n"
         "\n"
         "options:\n"
         "  --config PATH       path to behavior config TOML (default config.toml)\n"
         "  --env-file PATH     path to secrets .env file (default .env)\n";
}

struct ParsedArgs {
  std::string configPath = "config.toml";
  std::string envFilePath = ".env";
  std::string command;
  bool dryRun = false;
  bool showHelp = false;
  std::string error;
};

ParsedArgs parseArgs(const std::vector<std::string>& args) {
  ParsedArgs parsed;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "--help" || arg == "-h") {
      parsed.showHelp = true;
      return parsed;
    }
    if (arg == "--config" || arg == "--env-file") {
      if (i + 1 >= args.size()) {
        parsed.error = arg + " requires a value";
        return parsed;
      }
      (arg == "--config" ? parsed.configPath : parsed.envFilePath) = args[++i];
    } else if (arg == "--dry-run") {
      if (parsed.command != "once") {
        parsed.error = "--dry-run is only valid after the once command";
        return parsed;
      }
      parsed.dryRun = true;
    } else if (parsed.command.empty()) {
      parsed.command = arg;
    } else {
      parsed.error = "unexpected argument: " + arg;
      return parsed;
    }
  }
  if (parsed.command.empty()) {
    parsed.error = "a command is required";
  } else if (parsed.command != "once" && parsed.command != "watch" &&
             parsed.command != "test-notification") {
    parsed.error = "unknown command: " + parsed.command;
  }
  return parsed;
}

}  // namespace

std::unique_ptr<LaunchScheduleSource> defaultBuildSource(const Config&) {
  return std::make_unique<LaunchLibrarySource>();
}

std::unique_ptr<NotificationChannel> defaultBuildChannel(const Config&,
                                                         const PushoverSecrets& secrets) {
  PushoverChannel::Options options;
  options.token = secrets.token;
  options.userKeys = secrets.userKeys;
  return std::make_unique<PushoverChannel>(std::move(options));
}

int runMain(const std::vector<std::string>& args, const CliDeps& deps) {
  ParsedArgs parsed = parseArgs(args);
  if (parsed.showHelp) {
    printUsage(*deps.out);
    return 0;
  }
  if (!parsed.error.empty()) {
    *deps.err << "error: " << parsed.error << "\n\n";
    printUsage(*deps.err);
    return 2;
  }

  loadDotenv(parsed.envFilePath);
  try {
    Config config = loadConfig(parsed.configPath);
    if (parsed.command == "once") {
      return commandOnce(config, parsed.dryRun, deps);
    }
    if (parsed.command == "watch") {
      return commandWatch(config, deps);
    }
    return commandTestNotification(config, deps);
  } catch (const ConfigError& error) {
    *deps.err << "configuration error: " << error.what() << "\n";
    return 2;
  }
}

}  // namespace rocket_watcher
