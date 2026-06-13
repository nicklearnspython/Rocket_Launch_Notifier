#include "rocket_watcher/storage.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "tests/test_helpers.hpp"

namespace rocket_watcher::testing {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;

std::vector<AlertRecord> sampleRecords() {
  Launch first_launch = makeLaunch();
  first_launch.sourceLaunchId = "a-1";
  AlertRecord first = makeRecord(first_launch, {AlertKind::kLaunchSoon});

  Launch second_launch = makeLaunch();
  second_launch.sourceLaunchId = "b-2";
  second_launch.launchTime = std::nullopt;
  AlertRecord second =
      makeRecord(second_launch,
                 std::vector<SentAlert>{
                     SentAlert{AlertKind::kHourPrecision, kNow - 2h, std::nullopt},
                     SentAlert{AlertKind::kCorrection, kNow, CorrectionReason::kTimingNowImprecise},
                 });
  return {first, second};
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream file(path);
  std::ostringstream content;
  content << file.rdbuf();
  return content.str();
}

TEST(Storage, MissingFileLoadsAsEmpty) {
  TempDir dir;
  EXPECT_THAT(JsonAlertRecordStore(dir / "records.json").load(), IsEmpty());
}

TEST(Storage, SaveAndLoadRoundtrip) {
  TempDir dir;
  JsonAlertRecordStore store(dir.path() / "data" / "records.json");
  std::vector<AlertRecord> records = sampleRecords();
  store.save(records);
  std::vector<AlertRecord> loaded = store.load();
  auto by_key = [](const AlertRecord& a, const AlertRecord& b) { return a.key < b.key; };
  std::sort(loaded.begin(), loaded.end(), by_key);
  std::sort(records.begin(), records.end(), by_key);
  EXPECT_EQ(loaded, records);
}

TEST(Storage, TimestampsSerializedAsUtcIso) {
  TempDir dir;
  std::filesystem::path path = dir / "records.json";
  JsonAlertRecordStore store(path);
  store.save({sampleRecords()[0]});
  nlohmann::json raw = nlohmann::json::parse(readFile(path));
  const nlohmann::json& record = raw["records"][0];
  EXPECT_THAT(record["first_alert_at"].get<std::string>(), ::testing::EndsWith("+00:00"));
  EXPECT_THAT(record["last_facts"]["launch_time"].get<std::string>(),
              ::testing::EndsWith("+00:00"));
}

TEST(Storage, RecordShapeIsReadable) {
  TempDir dir;
  std::filesystem::path path = dir / "records.json";
  JsonAlertRecordStore(path).save(sampleRecords());
  std::string text = readFile(path);
  EXPECT_THAT(text, HasSubstr("\n  "));  // indented, human-inspectable
  nlohmann::json raw = nlohmann::json::parse(text);
  EXPECT_EQ(raw["version"], 1);
  const nlohmann::json& record = raw["records"][0];
  for (const char* field : {"key", "source_name", "source_launch_id", "launch_name",
                            "matched_terms", "last_facts", "sent_alerts", "last_alert_at"}) {
    EXPECT_TRUE(record.contains(field)) << "missing field: " << field;
  }
  EXPECT_EQ(record["key"], record["source_name"].get<std::string>() + ":" +
                               record["source_launch_id"].get<std::string>());
}

TEST(Storage, SaveLeavesNoTempFile) {
  TempDir dir;
  JsonAlertRecordStore(dir / "records.json").save(sampleRecords());
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(dir.path())) {
    names.push_back(entry.path().filename().string());
  }
  EXPECT_THAT(names, ::testing::ElementsAre("records.json"));
}

// Simulates a rename failure (e.g. disk full) at the atomic-replace step.
class FailingReplaceStore : public JsonAlertRecordStore {
 public:
  using JsonAlertRecordStore::JsonAlertRecordStore;

 protected:
  void replaceFile(const std::filesystem::path&, const std::filesystem::path&) override {
    throw std::runtime_error("disk full");
  }
};

TEST(Storage, FailedSavePreservesExistingFile) {
  TempDir dir;
  std::filesystem::path path = dir / "records.json";
  JsonAlertRecordStore good_store(path);
  good_store.save({sampleRecords()[0]});
  std::string before = readFile(path);

  FailingReplaceStore failing_store(path);
  EXPECT_THROW(failing_store.save(sampleRecords()), StorageError);
  EXPECT_EQ(readFile(path), before);
  EXPECT_FALSE(std::filesystem::exists(dir / "records.json.tmp"));
}

TEST(Storage, CorruptFileRaisesStorageError) {
  TempDir dir;
  std::filesystem::path path = dir / "records.json";
  std::ofstream(path) << "{not json";
  EXPECT_THROW(JsonAlertRecordStore(path).load(), StorageError);
}

}  // namespace
}  // namespace rocket_watcher::testing
