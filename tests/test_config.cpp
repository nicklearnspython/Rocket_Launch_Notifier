#include "rocket_watcher/config.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <map>

#include "tests/test_helpers.hpp"

namespace rocket_watcher::testing {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

const std::string kValid = R"(
[relevance]
launch_provider = "SpaceX"
include_terms = ["Starlink", "Starship"]

[pushover]
token_env = "PUSHOVER_TOKEN"

[[recipients]]
name = "alice"
user_key_env = "PUSHOVER_USER_ALICE"

[[recipients]]
name = "bob"
user_key_env = "PUSHOVER_USER_BOB"
)";

std::string replaceOnce(std::string text, const std::string& from, const std::string& to) {
  size_t position = text.find(from);
  EXPECT_NE(position, std::string::npos) << "fragment not found: " << from;
  if (position != std::string::npos) text.replace(position, from.size(), to);
  return text;
}

std::filesystem::path writeConfig(const TempDir& dir, const std::string& text) {
  std::filesystem::path path = dir / "config.toml";
  std::ofstream(path) << text;
  return path;
}

EnvLookup mapEnv(std::map<std::string, std::string> values) {
  return [values = std::move(values)](const std::string& name) -> std::optional<std::string> {
    auto found = values.find(name);
    if (found == values.end()) return std::nullopt;
    return found->second;
  };
}

void setTestEnv(const std::string& key, const std::string& value) {
#ifdef _WIN32
  _putenv_s(key.c_str(), value.c_str());
#else
  ::setenv(key.c_str(), value.c_str(), 1);
#endif
}

void unsetTestEnv(const std::string& key) {
#ifdef _WIN32
  _putenv_s(key.c_str(), "");
#else
  ::unsetenv(key.c_str());
#endif
}

TEST(Config, ValidConfigLoadsWithDefaults) {
  TempDir dir;
  Config config = loadConfig(writeConfig(dir, kValid));
  EXPECT_EQ(config.launchProvider, "SpaceX");
  EXPECT_THAT(config.includeTerms, ElementsAre("Starlink", "Starship"));
  ASSERT_EQ(config.recipients.size(), 2u);
  EXPECT_EQ(config.recipients[0].name, "alice");
  EXPECT_EQ(config.recipients[1].name, "bob");
  EXPECT_EQ(config.pushoverTokenEnv, "PUSHOVER_TOKEN");
  // Documented defaults.
  EXPECT_EQ(config.launchSoonMinutes, 60);
  EXPECT_EQ(config.launchImminentMinutes, 10);
  EXPECT_EQ(config.correctionTimeShiftMinutes, 30);
  EXPECT_EQ(config.scheduleLookaheadHours, 30);
  EXPECT_EQ(config.pollIntervalMinutes, 5);
  EXPECT_EQ(config.retentionDays, 30);
  EXPECT_EQ(config.displayTimezone, "America/Los_Angeles");
}

TEST(Config, ExplicitValuesOverrideDefaults) {
  TempDir dir;
  std::string text = kValid + R"(
[alerts]
launch_soon_minutes = 90
launch_imminent_minutes = 15
correction_time_shift_minutes = 45

[watcher]
poll_interval_minutes = 10
retention_days = 14
display_timezone = "UTC"
)";
  Config config = loadConfig(writeConfig(dir, text));
  EXPECT_EQ(config.launchSoonMinutes, 90);
  EXPECT_EQ(config.launchImminentMinutes, 15);
  EXPECT_EQ(config.correctionTimeShiftMinutes, 45);
  EXPECT_EQ(config.pollIntervalMinutes, 10);
  EXPECT_EQ(config.retentionDays, 14);
  EXPECT_EQ(config.displayTimezone, "UTC");
}

struct InvalidConfigCase {
  std::string name;
  std::string broken;
  std::string fragment;
};

class InvalidConfigs : public ::testing::TestWithParam<InvalidConfigCase> {};

TEST_P(InvalidConfigs, FailStartupValidation) {
  TempDir dir;
  const InvalidConfigCase& test_case = GetParam();
  try {
    loadConfig(writeConfig(dir, test_case.broken));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_THAT(error.what(), HasSubstr(test_case.fragment));
  }
}

INSTANTIATE_TEST_SUITE_P(
    Config, InvalidConfigs,
    ::testing::Values(
        InvalidConfigCase{"EmptyIncludeTerms",
                          replaceOnce(kValid, "include_terms = [\"Starlink\", \"Starship\"]",
                                      "include_terms = []"),
                          "include_terms"},
        InvalidConfigCase{"MissingIncludeTerms",
                          replaceOnce(kValid, "include_terms = [\"Starlink\", \"Starship\"]\n", ""),
                          "include_terms"},
        InvalidConfigCase{"BlankIncludeTerm",
                          replaceOnce(kValid, "include_terms = [\"Starlink\", \"Starship\"]",
                                      "include_terms = [\"  \"]"),
                          "non-empty"},
        InvalidConfigCase{"MissingLaunchProvider",
                          replaceOnce(kValid, "launch_provider = \"SpaceX\"\n", ""),
                          "launch_provider"},
        InvalidConfigCase{"MissingTokenEnv",
                          replaceOnce(kValid, "token_env = \"PUSHOVER_TOKEN\"\n", ""), "token_env"},
        InvalidConfigCase{"NoRecipients",
                          replaceOnce(replaceOnce(kValid, "[[recipients]]", "[[old_recipients]]"),
                                      "[[recipients]]", "[[old_recipients]]"),
                          "recipients"},
        InvalidConfigCase{"MissingUserKeyEnv",
                          replaceOnce(kValid, "user_key_env = \"PUSHOVER_USER_ALICE\"\n", ""),
                          "user_key_env"},
        InvalidConfigCase{"BadTimezone", kValid + "\n[watcher]\ndisplay_timezone = \"Not/AZone\"\n",
                          "timezone"},
        InvalidConfigCase{"ImminentNotLessThanSoon",
                          kValid + "\n[alerts]\nlaunch_imminent_minutes = 60\n", "less than"},
        InvalidConfigCase{"NonPositivePollInterval",
                          kValid + "\n[watcher]\npoll_interval_minutes = 0\n", "positive"}),
    [](const ::testing::TestParamInfo<InvalidConfigCase>& info) { return info.param.name; });

TEST(Config, MissingConfigFileIsAConfigError) {
  TempDir dir;
  EXPECT_THROW(loadConfig(dir / "nope.toml"), ConfigError);
}

TEST(Config, ResolveSecretsReadsNamedEnvVars) {
  TempDir dir;
  Config config = loadConfig(writeConfig(dir, kValid));
  PushoverSecrets secrets = resolveSecrets(config, mapEnv({
                                                       {"PUSHOVER_TOKEN", "tok"},
                                                       {"PUSHOVER_USER_ALICE", "key-a"},
                                                       {"PUSHOVER_USER_BOB", "key-b"},
                                                   }));
  EXPECT_EQ(secrets.token, "tok");
  std::map<std::string, std::string> expected = {{"alice", "key-a"}, {"bob", "key-b"}};
  EXPECT_EQ(secrets.userKeys, expected);
}

TEST(Config, ResolveSecretsReportsAllMissingEnvVars) {
  TempDir dir;
  Config config = loadConfig(writeConfig(dir, kValid));
  try {
    resolveSecrets(config, mapEnv({{"PUSHOVER_USER_ALICE", "key-a"}}));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_THAT(error.what(), HasSubstr("PUSHOVER_TOKEN"));
    EXPECT_THAT(error.what(), HasSubstr("PUSHOVER_USER_BOB"));
  }
}

TEST(Config, LoadDotenvSetsWithoutOverriding) {
  TempDir dir;
  setTestEnv("ALREADY_SET", "original");
  unsetTestEnv("DOTENV_ONLY");
  unsetTestEnv("QUOTED");
  std::filesystem::path env_file = dir / ".env";
  std::ofstream(env_file) << "# comment\nDOTENV_ONLY=value1\nALREADY_SET=overridden\n"
                             "QUOTED='value2'\n";
  loadDotenv(env_file);
  ASSERT_NE(std::getenv("DOTENV_ONLY"), nullptr);
  EXPECT_STREQ(std::getenv("DOTENV_ONLY"), "value1");
  EXPECT_STREQ(std::getenv("ALREADY_SET"), "original");
  ASSERT_NE(std::getenv("QUOTED"), nullptr);
  EXPECT_STREQ(std::getenv("QUOTED"), "value2");
  unsetTestEnv("DOTENV_ONLY");
  unsetTestEnv("QUOTED");
  unsetTestEnv("ALREADY_SET");
}

TEST(Config, LoadDotenvMissingFileIsNoop) {
  TempDir dir;
  loadDotenv(dir / "missing.env");  // must not throw
}

}  // namespace
}  // namespace rocket_watcher::testing
