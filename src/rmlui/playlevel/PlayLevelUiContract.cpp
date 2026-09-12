#include "PlayLevelUiContract.hpp"

#include <algorithm>
#include <cmath>

namespace NodeSpireUi {
namespace {
float boundedUnit(float value) {
    return std::max(0.0f, std::min(value, 1.0f));
}

float progress(float remaining, float duration) {
    if (duration <= 0.0f) {
        return 0.0f;
    }
    return boundedUnit(remaining / duration);
}
} // namespace

PlayLevelUiSnapshot buildPlayLevelUiSnapshot(const PlayLevelUiStateInput& input) {
    PlayLevelUiSnapshot view;
    view.phase = input.phase;
    view.levelName = input.levelName;
    view.loadingActivity = input.loadingActivity;
    view.loadingProgress = boundedUnit(input.loadingProgress);
    view.baseHealth = std::max(0, input.baseHealth);
    view.money = std::max(0, input.money);
    view.currentWave = std::max(0, input.currentWave);
    view.waveCount = std::max(0, input.waveCount);
    view.enemiesAlive = std::max(0, input.enemiesAlive);
    view.enemiesRemaining = std::max(0, input.enemiesAlive + input.enemiesToSpawn);

    switch (input.phase) {
    case PlayLevelUiPhase::Loading:
        view.headline = "Loading " + input.levelName;
        view.supportingText = input.loadingActivity.empty() ? "Preparing the battlefield..." : input.loadingActivity;
        break;
    case PlayLevelUiPhase::LoadFailed:
        view.headline = "Deployment failed";
        view.supportingText = input.loadingActivity.empty() ? "The level could not be loaded." : input.loadingActivity;
        break;
    case PlayLevelUiPhase::WaitingToStart:
        view.headline = "Prepare your defenses";
        view.supportingText = "Deploy when your defenses are ready.";
        view.startWaveVisible = true;
        view.startWaveEnabled = input.worldReady && input.routeReady && input.enemyTemplateReady;
        if (!input.worldReady) {
            view.startWaveDisabledReason = "The battlefield is still loading.";
        } else if (!input.routeReady) {
            view.startWaveDisabledReason = "This level has no valid enemy route.";
        } else if (!input.enemyTemplateReady) {
            view.startWaveDisabledReason = "Enemy assets are not ready.";
        }
        break;
    case PlayLevelUiPhase::Running:
        view.headline = "Defend the Spire";
        break;
    case PlayLevelUiPhase::Paused:
        view.headline = "Paused";
        view.supportingText = "The battle is waiting for you.";
        break;
    case PlayLevelUiPhase::Victory:
        view.headline = "Victory";
        view.supportingText = "All waves are cleared.";
        break;
    case PlayLevelUiPhase::Defeat:
        view.headline = "Defeated";
        view.supportingText = "Your base has fallen.";
        break;
    }

    view.loadoutVisible = input.phase == PlayLevelUiPhase::WaitingToStart || input.phase == PlayLevelUiPhase::Running;
    if (input.phase == PlayLevelUiPhase::Running && input.preWaveCountdownActive) {
        view.countdownVisible = true;
        view.countdownLabel = "Next wave in";
        view.countdownSeconds = std::max(0, static_cast<int>(std::ceil(input.preWaveCountdownRemainingSeconds)));
        view.countdownProgress = progress(input.preWaveCountdownRemainingSeconds, input.preWaveCountdownDurationSeconds);
    } else if (input.phase == PlayLevelUiPhase::Running && !input.waveInProgress &&
               input.roundCountdownRemainingSeconds > 0.0f) {
        view.countdownVisible = true;
        view.countdownLabel = "Wave " + std::to_string(input.currentWave + 1) + " in";
        view.countdownSeconds = std::max(0, static_cast<int>(std::ceil(input.roundCountdownRemainingSeconds)));
        view.countdownProgress = progress(input.roundCountdownRemainingSeconds, input.roundCountdownDurationSeconds);
    }

    return view;
}

} // namespace NodeSpireUi