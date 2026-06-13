#include "rocket_watcher/relevance.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tests/test_helpers.hpp"

namespace rocket_watcher::testing {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

TEST(Relevance, MatchingProviderAndTermIsRelevant) {
  RelevanceResult result = evaluateRelevance(makeLaunch(), "SpaceX", {"Starlink"});
  EXPECT_TRUE(result.relevant);
  EXPECT_THAT(result.matchedTerms, ElementsAre("Starlink"));
}

TEST(Relevance, ProviderMatchIsCaseInsensitive) {
  Launch launch = makeLaunch();
  launch.provider = "SPACEX";
  RelevanceResult result = evaluateRelevance(launch, "spacex", {"starlink"});
  EXPECT_TRUE(result.relevant);
}

TEST(Relevance, IncludeTermMatchIsCaseInsensitive) {
  RelevanceResult result = evaluateRelevance(makeLaunch(), "SpaceX", {"sTaRlInK"});
  EXPECT_TRUE(result.relevant);
  EXPECT_THAT(result.matchedTerms, ElementsAre("sTaRlInK"));
}

TEST(Relevance, ProviderMismatchIsNotRelevantEvenWithTermMatch) {
  Launch launch = makeLaunch();
  launch.provider = "Rocket Lab";
  RelevanceResult result = evaluateRelevance(launch, "SpaceX", {"Starlink"});
  EXPECT_FALSE(result.relevant);
  EXPECT_THAT(result.reason, HasSubstr("provider"));
}

TEST(Relevance, SpacexLaunchWithoutMatchingTermIsNotRelevant) {
  Launch launch = makeLaunch();
  launch.name = "Falcon Heavy | GOES-U";
  launch.missionName = "GOES-U";
  launch.missionDescription = "A weather satellite.";
  launch.padName = "LC-39A, Kennedy Space Center";
  RelevanceResult result = evaluateRelevance(launch, "SpaceX", {"Starship"});
  EXPECT_FALSE(result.relevant);
  EXPECT_THAT(result.reason, HasSubstr("no include term"));
}

TEST(Relevance, TermsMatchAcrossMultipleSearchableFields) {
  Launch launch = makeLaunch();
  launch.name = "Falcon 9 | Mystery Mission";
  launch.missionName = std::nullopt;
  launch.missionDescription = "Deploys a Starlink shell to LEO.";
  launch.padName = "Starbase, Texas";
  RelevanceResult result = evaluateRelevance(launch, "SpaceX", {"Starlink", "Starbase", "Dragon"});
  EXPECT_TRUE(result.relevant);
  EXPECT_THAT(result.matchedTerms, ElementsAre("Starlink", "Starbase"));
}

TEST(Relevance, MissingOptionalFieldsAreSkipped) {
  Launch launch = makeLaunch();
  launch.missionName = std::nullopt;
  launch.missionDescription = std::nullopt;
  launch.padName = std::nullopt;
  RelevanceResult result = evaluateRelevance(launch, "SpaceX", {"Starlink"});
  EXPECT_TRUE(result.relevant);  // matched in the launch name
}

TEST(Relevance, EmptyIncludeTermsThrow) {
  EXPECT_THROW(evaluateRelevance(makeLaunch(), "SpaceX", {}), std::invalid_argument);
}

}  // namespace
}  // namespace rocket_watcher::testing
