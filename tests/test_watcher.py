from __future__ import annotations

from datetime import UTC, datetime
import unittest

from spacex_launch_watcher.watcher import Launch, is_relevant_launch


class WatcherTests(unittest.TestCase):
    def test_relevant_launch_requires_configured_provider_and_any_include_term_match(
        self,
    ) -> None:
        launch = Launch(
            name="Starship Flight Test",
            provider="SpaceX",
            launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
        )

        self.assertTrue(
            is_relevant_launch(
                launch, launch_provider="SpaceX", include_terms=("Starship",)
            )
        )
        self.assertFalse(
            is_relevant_launch(
                launch, launch_provider="Rocket Lab", include_terms=("Starship",)
            )
        )
        # This launch is a Starship launch, but it is not from Vandenberg.
        # Provider alone is not enough; the configured Include Term must match too.
        self.assertFalse(
            is_relevant_launch(
                launch, launch_provider="SpaceX", include_terms=("Vandenberg",)
            )
        )

    def test_starship_launches_match_any_location_when_starship_is_configured(
        self,
    ) -> None:
        launch = Launch(
            name="Starship Flight Test",
            provider="SpaceX",
            launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
            pad_location_name="Starbase Orbital Launch Pad",
        )

        self.assertTrue(
            is_relevant_launch(
                launch,
                launch_provider="SpaceX",
                include_terms=("Starship", "Vandenberg"),
            )
        )

    def test_falcon_9_launches_match_when_launching_from_vandenberg(self) -> None:
        launch = Launch(
            name="Falcon 9 Block 5",
            provider="SpaceX",
            launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
            pad_location_name="Vandenberg Space Force Base",
        )

        self.assertTrue(
            is_relevant_launch(
                launch,
                launch_provider="SpaceX",
                include_terms=("Starship", "Vandenberg"),
            )
        )

    def test_relevance_matching_is_case_insensitive_across_searchable_fields(
        self,
    ) -> None:
        cases = [
            Launch(
                name="STARSHIP Flight Test",
                provider="spacex",
                launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
            ),
            Launch(
                name="Transporter",
                provider="spacex",
                launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
                mission_name="Starship payload demonstration",
            ),
            Launch(
                name="Falcon 9",
                provider="spacex",
                launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
                mission_description="Mission from Boca Chica.",
            ),
            Launch(
                name="Falcon 9",
                provider="spacex",
                launch_time=datetime(2026, 5, 24, 12, 0, tzinfo=UTC),
                pad_location_name="Vandenberg Space Force Base",
            ),
        ]

        for launch in cases:
            with self.subTest(launch=launch):
                self.assertTrue(
                    is_relevant_launch(
                        launch,
                        launch_provider="SPACEX",
                        include_terms=("starship", "boca chica", "vandenberg"),
                    )
                )


if __name__ == "__main__":
    unittest.main()
