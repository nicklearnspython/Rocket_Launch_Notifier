#include "rocket_watcher/launch_library.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "rocket_watcher/time_utils.hpp"
#include "tests/test_helpers.hpp"

namespace rocket_watcher::testing {
namespace {

using nlohmann::json;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

json ll2Item() {
  return {
      {"id", "uuid-1234"},
      {"name", "Falcon 9 Block 5 | Starlink Group 12-34"},
      {"net", "2026-06-09T18:45:00Z"},
      {"net_precision", {{"name", "Minute"}}},
      {"status", {{"abbrev", "Go"}, {"name", "Go for Launch"}}},
      {"launch_service_provider", {{"name", "SpaceX"}}},
      {"mission",
       {{"name", "Starlink Group 12-34"}, {"description", "A batch of Starlink satellites."}}},
      {"pad", {{"name", "SLC-40"}, {"location", {{"name", "Cape Canaveral SFS, FL, USA"}}}}},
  };
}

struct SourceFixture {
  explicit SourceFixture(std::vector<json> items) {
    LaunchLibrarySource::Options options;
    options.httpGet = [this, items = std::move(items)](const std::string& url,
                                                       std::chrono::seconds) -> std::string {
      capturedUrl = url;
      return json{{"results", items}}.dump();
    };
    options.nowFn = [] { return kNow; };
    source = std::make_unique<LaunchLibrarySource>(std::move(options));
  }

  std::unique_ptr<LaunchLibrarySource> source;
  std::string capturedUrl;
};

TEST(LaunchLibrary, MapsContractFields) {
  SourceFixture fixture({ll2Item()});
  FetchResult result = fixture.source->fetch();
  ASSERT_EQ(result.launches.size(), 1u);
  const Launch& launch = result.launches[0];
  EXPECT_EQ(launch.sourceName, "ll2");
  EXPECT_EQ(launch.sourceLaunchId, "uuid-1234");
  EXPECT_EQ(launch.key(), "ll2:uuid-1234");
  EXPECT_EQ(launch.name, "Falcon 9 Block 5 | Starlink Group 12-34");
  EXPECT_EQ(launch.provider, "SpaceX");
  EXPECT_EQ(launch.launchTime,
            std::chrono::sys_days{std::chrono::year{2026} / 6 / 9} + 18h + 45min);
  EXPECT_EQ(launch.precision, TimingPrecision::kPrecise);
  EXPECT_EQ(launch.status, LaunchStatus::kGo);
  ASSERT_TRUE(launch.missionDescription.has_value());
  EXPECT_THAT(*launch.missionDescription, HasSubstr("Starlink satellites"));
  EXPECT_EQ(launch.padName, "SLC-40, Cape Canaveral SFS, FL, USA");
  EXPECT_THAT(result.warnings, IsEmpty());
  EXPECT_THAT(fixture.capturedUrl, HasSubstr("launch/upcoming/"));
}

struct PrecisionCase {
  std::string name;
  std::string ll2Precision;
  TimingPrecision expected;
};

class PrecisionMapping : public ::testing::TestWithParam<PrecisionCase> {};

TEST_P(PrecisionMapping, MapsToProjectPrecision) {
  json item = ll2Item();
  item["net_precision"] = {{"name", GetParam().ll2Precision}};
  SourceFixture fixture({item});
  FetchResult result = fixture.source->fetch();
  ASSERT_EQ(result.launches.size(), 1u);
  EXPECT_EQ(result.launches[0].precision, GetParam().expected);
}

INSTANTIATE_TEST_SUITE_P(
    LaunchLibrary, PrecisionMapping,
    ::testing::Values(PrecisionCase{"Second", "Second", TimingPrecision::kPrecise},
                      PrecisionCase{"Minute", "Minute", TimingPrecision::kPrecise},
                      PrecisionCase{"Hour", "Hour", TimingPrecision::kHour},
                      PrecisionCase{"Day", "Day", TimingPrecision::kImprecise},
                      PrecisionCase{"Month", "Month", TimingPrecision::kImprecise},
                      // Calendar-window names seen in live LL2 data.
                      PrecisionCase{"Quarter2", "Quarter 2", TimingPrecision::kImprecise},
                      PrecisionCase{"YearHalf1", "Year Half 1", TimingPrecision::kImprecise},
                      PrecisionCase{"FiscalYear2027", "Fiscal Year 2027",
                                    TimingPrecision::kImprecise}),
    [](const ::testing::TestParamInfo<PrecisionCase>& info) { return info.param.name; });

struct StatusCase {
  std::string name;
  std::string abbrev;
  LaunchStatus expected;
};

class StatusMapping : public ::testing::TestWithParam<StatusCase> {};

TEST_P(StatusMapping, MapsToProjectStatus) {
  json item = ll2Item();
  item["status"] = {{"abbrev", GetParam().abbrev}};
  SourceFixture fixture({item});
  FetchResult result = fixture.source->fetch();
  ASSERT_EQ(result.launches.size(), 1u);
  EXPECT_EQ(result.launches[0].status, GetParam().expected);
}

INSTANTIATE_TEST_SUITE_P(
    LaunchLibrary, StatusMapping,
    ::testing::Values(StatusCase{"Go", "Go", LaunchStatus::kGo},
                      StatusCase{"Hold", "Hold", LaunchStatus::kNoGo},
                      StatusCase{"Success", "Success", LaunchStatus::kEnded},
                      StatusCase{"Failure", "Failure", LaunchStatus::kEnded},
                      StatusCase{"InFlight", "In Flight", LaunchStatus::kEnded},
                      StatusCase{"TBC", "TBC", LaunchStatus::kUnknown},
                      StatusCase{"TBD", "TBD", LaunchStatus::kUnknown}),
    [](const ::testing::TestParamInfo<StatusCase>& info) { return info.param.name; });

TEST(LaunchLibrary, UnmappedPrecisionWarnsAndFallsBackToUnknown) {
  json item = ll2Item();
  item["net_precision"] = {{"name", "Fortnight"}};
  SourceFixture fixture({item});
  FetchResult result = fixture.source->fetch();
  EXPECT_EQ(result.launches[0].precision, TimingPrecision::kUnknown);
  EXPECT_THAT(result.warnings, Contains(AllOf(HasSubstr("Fortnight"), HasSubstr("precision"))));
}

TEST(LaunchLibrary, UnmappedStatusWarnsAndFallsBackToUnknown) {
  json item = ll2Item();
  item["status"] = {{"abbrev", "Mystery"}};
  SourceFixture fixture({item});
  FetchResult result = fixture.source->fetch();
  EXPECT_EQ(result.launches[0].status, LaunchStatus::kUnknown);
  EXPECT_THAT(result.warnings, Contains(AllOf(HasSubstr("Mystery"), HasSubstr("status"))));
}

TEST(LaunchLibrary, MissingNetAndPrecisionWarnAndAreUnknown) {
  json item = ll2Item();
  item["net"] = nullptr;
  item["net_precision"] = nullptr;
  SourceFixture fixture({item});
  FetchResult result = fixture.source->fetch();
  ASSERT_EQ(result.launches.size(), 1u);
  const Launch& launch = result.launches[0];
  EXPECT_FALSE(launch.launchTime.has_value());
  EXPECT_EQ(launch.precision, TimingPrecision::kUnknown);
  EXPECT_FALSE(result.warnings.empty());  // human-readable warning recorded
}

TEST(LaunchLibrary, MissingMissionAndPadAreTolerated) {
  json item = ll2Item();
  item["mission"] = nullptr;
  item["pad"] = nullptr;
  SourceFixture fixture({item});
  FetchResult result = fixture.source->fetch();
  ASSERT_EQ(result.launches.size(), 1u);
  const Launch& launch = result.launches[0];
  EXPECT_FALSE(launch.missionName.has_value());
  EXPECT_FALSE(launch.padName.has_value());
  EXPECT_THAT(launch.searchableFields(), ::testing::ElementsAre(launch.name));
}

TEST(LaunchLibrary, HttpFailureRaisesSourceError) {
  LaunchLibrarySource::Options options;
  options.httpGet = [](const std::string&, std::chrono::seconds) -> std::string {
    throw std::runtime_error("connection refused");
  };
  LaunchLibrarySource source(std::move(options));
  EXPECT_THROW(source.fetch(), SourceError);
}

}  // namespace
}  // namespace rocket_watcher::testing
