#include "../../src/rmlui/playlevel/PlayLevelUiContract.hpp"

#include <gtest/gtest.h>

namespace NodeSpireUi {

TEST(PlayLevelUiContract, WaitingStateExplainsWhyStartIsUnavailable) {
    PlayLevelUiStateInput input;
    input.phase = PlayLevelUiPhase::WaitingToStart;
    input.levelName = "Grassy";
    input.worldReady = true;
    input.routeReady = false;
    input.enemyTemplateReady = true;

    const PlayLevelUiSnapshot view = buildPlayLevelUiSnapshot(input);

    EXPECT_TRUE(view.startWaveVisible);
    EXPECT_FALSE(view.startWaveEnabled);
    EXPECT_EQ(view.startWaveDisabledReason, "This level has no valid enemy route.");
    EXPECT_TRUE(view.loadoutVisible);
}

TEST(PlayLevelUiContract, WaitingStateEnablesStartOnlyWhenEnginePrerequisitesAreReady) {
    PlayLevelUiStateInput input;
    input.phase = PlayLevelUiPhase::WaitingToStart;
    input.worldReady = true;
    input.routeReady = true;
    input.enemyTemplateReady = true;

    const PlayLevelUiSnapshot view = buildPlayLevelUiSnapshot(input);

    EXPECT_TRUE(view.startWaveEnabled);
    EXPECT_TRUE(view.startWaveDisabledReason.empty());
}

TEST(PlayLevelUiContract, RunningStateBuildsPreWaveCountdownPresentation) {
    PlayLevelUiStateInput input;
    input.phase = PlayLevelUiPhase::Running;
    input.preWaveCountdownActive = true;
    input.preWaveCountdownRemainingSeconds = 2.2f;
    input.preWaveCountdownDurationSeconds = 5.0f;

    const PlayLevelUiSnapshot view = buildPlayLevelUiSnapshot(input);

    EXPECT_TRUE(view.countdownVisible);
    EXPECT_EQ(view.countdownLabel, "Next wave in");
    EXPECT_EQ(view.countdownSeconds, 3);
    EXPECT_FLOAT_EQ(view.countdownProgress, 0.44f);
}

TEST(PlayLevelUiContract, RoundBreakNamesTheUpcomingWave) {
    PlayLevelUiStateInput input;
    input.phase = PlayLevelUiPhase::Running;
    input.currentWave = 4;
    input.waveInProgress = false;
    input.roundCountdownRemainingSeconds = 8.0f;
    input.roundCountdownDurationSeconds = 10.0f;

    const PlayLevelUiSnapshot view = buildPlayLevelUiSnapshot(input);

    EXPECT_TRUE(view.countdownVisible);
    EXPECT_EQ(view.countdownLabel, "Wave 5 in");
    EXPECT_EQ(view.countdownSeconds, 8);
    EXPECT_FLOAT_EQ(view.countdownProgress, 0.8f);
}

TEST(PlayLevelUiContract, TerminalStatesHideGameplayControls) {
    PlayLevelUiStateInput input;
    input.phase = PlayLevelUiPhase::Defeat;

    const PlayLevelUiSnapshot view = buildPlayLevelUiSnapshot(input);

    EXPECT_EQ(view.headline, "Defeated");
    EXPECT_FALSE(view.loadoutVisible);
    EXPECT_FALSE(view.startWaveVisible);
    EXPECT_FALSE(view.countdownVisible);
}

} // namespace NodeSpireUi