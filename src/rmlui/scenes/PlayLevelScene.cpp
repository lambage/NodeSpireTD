#include "rmlui/scenes/PlayLevelScene.hpp"

#include "RmlUi_Backend.h"
#include "AudioEngine.hpp"
#include "VulkanContext.hpp"
#include "multiplayer/MultiplayerSession.hpp"
#include "utility/WorldRenderer.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace NodeSpireUi {
namespace {
constexpr const char* kDocumentPath = "assets/ui/playlevel/playlevel.rml";
constexpr const char* kInteractiveIds[] = {"retry-button", "start-match-button", "resume-button", "back-to-lobby-button",
                                            "master-volume-slider", "music-volume-slider", "sfx-volume-slider"};

glm::vec3 cameraForward(float yaw, float pitch) {
    return glm::normalize(glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                    std::cos(pitch) * std::cos(yaw)));
}

void setText(Rml::ElementDocument* document, const char* id, const std::string& value) {
    if (Rml::Element* element = document->GetElementById(id)) {
        element->SetInnerRML(value);
    }
}
} // namespace

PlayLevelScene::PlayLevelScene(VulkanContext& vulkanContext, multiplayer::MultiplayerSession& session,
                               const PlayLevelLaunchConfig& launchConfig)
    : vulkanContext_(vulkanContext), session_(session), launchConfig_(launchConfig) {}

PlayLevelScene::~PlayLevelScene() = default;

void PlayLevelScene::onEnter(Rml::Context& context, AudioEngine& audio) {
    pendingTransition_.reset();
    pauseMenuVisible_ = false;
    onlineMatch_ = session_.isInParty();
    loadedReadySignaled_ = false;
    audio_ = &audio;
    settings_ = settingsManager_.loadOrCreateDefaults();
    document_ = context.LoadDocument(kDocumentPath);
    if (!document_) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", kDocumentPath);
        return;
    }

    for (const char* id : kInteractiveIds) {
        if (Rml::Element* element = document_->GetElementById(id)) {
            element->AddEventListener(Rml::EventId::Click, this);
            element->AddEventListener(Rml::EventId::Change, this);
        }
    }

    document_->Show();
    populateAudioControls();
    beginWorldLoad();
    refreshHud();
}

void PlayLevelScene::onExit(Rml::Context& context) {
    if (mouseLookActive_) {
        SDL_SetWindowRelativeMouseMode(Backend::GetWindow(), false);
        mouseLookActive_ = false;
    }

    if (document_) {
        for (const char* id : kInteractiveIds) {
            if (Rml::Element* element = document_->GetElementById(id)) {
                element->RemoveEventListener(Rml::EventId::Click, this);
                element->RemoveEventListener(Rml::EventId::Change, this);
            }
        }
        document_->Close();
        context.UnloadDocument(document_);
        document_ = nullptr;
    }

    vulkanContext_.waitIdle();
    worldRenderer_.reset();
}

void PlayLevelScene::beginWorldLoad() {
    snapshot_ = {};
    snapshot_.phase = PlayLevelUiPhase::Loading;
    snapshot_.levelName = launchConfig_.displayName;
    snapshot_.headline = "Loading " + launchConfig_.displayName;
    snapshot_.supportingText = "Preparing the battlefield...";

    if (launchConfig_.mapAssetPath.empty() || launchConfig_.displayName.empty()) {
        snapshot_.phase = PlayLevelUiPhase::LoadFailed;
        snapshot_.headline = "Deployment failed";
        snapshot_.supportingText = "The selected level configuration is incomplete.";
        worldRenderer_.reset();
        return;
    }

    WorldAssetSpec assetSpec;
    assetSpec.startModelPath = launchConfig_.startModelPath;
    assetSpec.endModelPath = launchConfig_.endModelPath;
    for (const std::string& modelPath : launchConfig_.animatedTemplateModelPaths) {
        assetSpec.animatedTemplateModelPaths.emplace_back(modelPath);
    }
    worldRenderer_ = std::make_unique<WorldRenderer>(nullptr, vulkanContext_);
    worldRenderer_->beginLoad(launchConfig_.mapAssetPath, assetSpec);
}

SceneTransition PlayLevelScene::update(float dt) {
    if (onlineMatch_ && !session_.isInParty()) {
        return SceneId::Lobby;
    }

    if (!pauseMenuVisible_) {
        updateCamera(dt);
    }

    if (snapshot_.phase == PlayLevelUiPhase::WaitingToStart && session_.isClient() && session_.isMatchStarted()) {
        PlayLevelUiStateInput input;
        input.phase = PlayLevelUiPhase::Running;
        input.levelName = launchConfig_.displayName;
        input.worldReady = true;
        snapshot_ = buildPlayLevelUiSnapshot(input);
        refreshHud();
    }

    if (worldRenderer_ && snapshot_.phase == PlayLevelUiPhase::Loading) {
        worldRenderer_->tickLoad();
        snapshot_.loadingProgress = worldRenderer_->loadProgress();
        snapshot_.loadingActivity = worldRenderer_->loadActivity();
        snapshot_.supportingText = snapshot_.loadingActivity.empty() ? "Preparing the battlefield..." : snapshot_.loadingActivity;

        if (worldRenderer_->loadFailed()) {
            snapshot_.phase = PlayLevelUiPhase::LoadFailed;
            snapshot_.headline = "Deployment failed";
            snapshot_.supportingText = worldRenderer_->statusMessage();
        } else if (worldRenderer_->isLoaded()) {
            PlayLevelUiStateInput input;
            input.phase = PlayLevelUiPhase::WaitingToStart;
            input.levelName = launchConfig_.displayName;
            input.worldReady = true;
            input.routeReady = !worldRenderer_->routePoints().empty();
            snapshot_ = buildPlayLevelUiSnapshot(input);
            if (onlineMatch_ && !loadedReadySignaled_) {
                session_.signalLocalLoadedReady();
                loadedReadySignaled_ = true;
            }
            if (session_.isClient() && session_.isMatchStarted()) {
                input.phase = PlayLevelUiPhase::Running;
                snapshot_ = buildPlayLevelUiSnapshot(input);
            }
        }
        refreshHud();
    }
    if (snapshot_.phase == PlayLevelUiPhase::WaitingToStart) {
        refreshHud();
    }

    SceneTransition transition = pendingTransition_;
    pendingTransition_.reset();
    return transition;
}

void PlayLevelScene::updateCamera(float dt) {
    SDL_Window* window = Backend::GetWindow();
    if (!window || !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
        return;
    }

    const SDL_MouseButtonFlags mouseButtons = SDL_GetMouseState(nullptr, nullptr);
    const bool wantsMouseLook = (mouseButtons & SDL_BUTTON_RMASK) != 0;
    if (wantsMouseLook != mouseLookActive_) {
        if (SDL_SetWindowRelativeMouseMode(window, wantsMouseLook)) {
            mouseLookActive_ = wantsMouseLook;
            SDL_GetRelativeMouseState(nullptr, nullptr);
        }
    }

    if (mouseLookActive_) {
        float mouseDeltaX = 0.0f;
        float mouseDeltaY = 0.0f;
        SDL_GetRelativeMouseState(&mouseDeltaX, &mouseDeltaY);
        cameraYaw_ -= mouseDeltaX * 0.003f;
        cameraPitch_ = glm::clamp(cameraPitch_ - mouseDeltaY * 0.003f, -1.48f, 1.48f);
    }

    const bool* keyboard = SDL_GetKeyboardState(nullptr);
    const float frameTime = std::min(dt, 0.1f);
    const float speedMultiplier = (keyboard[SDL_SCANCODE_LSHIFT] || keyboard[SDL_SCANCODE_RSHIFT]) ? 4.0f : 1.0f;
    const float movement = 8.0f * speedMultiplier * frameTime;
    const glm::vec3 forward = cameraForward(cameraYaw_, cameraPitch_);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    if (keyboard[SDL_SCANCODE_W]) cameraPosition_ += forward * movement;
    if (keyboard[SDL_SCANCODE_S]) cameraPosition_ -= forward * movement;
    if (keyboard[SDL_SCANCODE_A]) cameraPosition_ -= right * movement;
    if (keyboard[SDL_SCANCODE_D]) cameraPosition_ += right * movement;
    if (keyboard[SDL_SCANCODE_SPACE]) cameraPosition_.y += movement;
    if (keyboard[SDL_SCANCODE_Q]) cameraPosition_.y -= movement;
}

void PlayLevelScene::renderWorld(VkCommandBuffer commandBuffer, VkExtent2D extent) {
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return;
    }

    const glm::vec3 forward = cameraForward(cameraYaw_, cameraPitch_);
    const glm::mat4 view = glm::lookAt(cameraPosition_, cameraPosition_ + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    worldRenderer_->render(commandBuffer, extent, view);
}

SceneTransition PlayLevelScene::onKeyDown(Rml::Input::KeyIdentifier key) {
    if (key == Rml::Input::KI_ESCAPE) {
        setPauseMenuVisible(!pauseMenuVisible_);
    }
    return std::nullopt;
}

void PlayLevelScene::ProcessEvent(Rml::Event& event) {
    Rml::Element* target = event.GetTargetElement();
    if (!target) {
        return;
    }

    const Rml::String id = target->GetId();
    if (event == Rml::EventId::Change) {
        if (id == "master-volume-slider") {
            settings_.masterVolume = event.GetParameter<float>("value", settings_.masterVolume);
            setAudioValueLabel("master-volume-value", settings_.masterVolume);
        } else if (id == "music-volume-slider") {
            settings_.musicVolume = event.GetParameter<float>("value", settings_.musicVolume);
            setAudioValueLabel("music-volume-value", settings_.musicVolume);
        } else if (id == "sfx-volume-slider") {
            settings_.sfxVolume = event.GetParameter<float>("value", settings_.sfxVolume);
            setAudioValueLabel("sfx-volume-value", settings_.sfxVolume);
        } else {
            return;
        }
        audio_->setEffectiveSettings(settings_);
        settingsManager_.save(settings_);
        return;
    }

    if (event != Rml::EventId::Click) {
        return;
    }

    if (id == "retry-button") {
        vulkanContext_.waitIdle();
        worldRenderer_.reset();
        loadedReadySignaled_ = false;
        beginWorldLoad();
        refreshHud();
    } else if (id == "resume-button") {
        setPauseMenuVisible(false);
    } else if (id == "start-match-button") {
        if (!onlineMatch_ || session_.beginMatch()) {
            PlayLevelUiStateInput input;
            input.phase = PlayLevelUiPhase::Running;
            input.levelName = launchConfig_.displayName;
            input.worldReady = true;
            snapshot_ = buildPlayLevelUiSnapshot(input);
            refreshHud();
        }
    } else if (id == "back-to-lobby-button") {
        if (session_.isHost()) {
            session_.leaveParty();
        }
        pendingTransition_ = SceneId::Lobby;
    }
}

void PlayLevelScene::setPauseMenuVisible(bool visible) {
    pauseMenuVisible_ = visible;
    if (mouseLookActive_) {
        SDL_SetWindowRelativeMouseMode(Backend::GetWindow(), false);
        mouseLookActive_ = false;
    }
    if (document_) {
        if (Rml::Element* menu = document_->GetElementById("pause-menu")) {
            menu->SetClass("hidden", !visible);
        }
    }
}

void PlayLevelScene::setAudioValueLabel(const char* id, float value) {
    char label[16];
    std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(value * 100.0f + 0.5f));
    setText(document_, id, label);
}

void PlayLevelScene::populateAudioControls() {
    const struct VolumeControl {
        const char* sliderId;
        const char* valueId;
        float value;
    } controls[] = {{"master-volume-slider", "master-volume-value", settings_.masterVolume},
                    {"music-volume-slider", "music-volume-value", settings_.musicVolume},
                    {"sfx-volume-slider", "sfx-volume-value", settings_.sfxVolume}};
    for (const auto& control : controls) {
        if (Rml::Element* slider = document_->GetElementById(control.sliderId)) {
            slider->SetAttribute("value", control.value);
        }
        setAudioValueLabel(control.valueId, control.value);
    }
}

void PlayLevelScene::refreshHud() {
    if (!document_) {
        return;
    }

    setText(document_, "level-name", snapshot_.levelName);
    setText(document_, "status-headline", snapshot_.headline);
    setText(document_, "status-copy", snapshot_.supportingText);

    if (Rml::Element* progress = document_->GetElementById("loading-progress")) {
        progress->SetProperty("width", std::to_string(snapshot_.loadingProgress * 100.0f) + "%");
    }
    if (Rml::Element* retry = document_->GetElementById("retry-button")) {
        retry->SetClass("visible", snapshot_.phase == PlayLevelUiPhase::LoadFailed);
    }
    if (Rml::Element* ready = document_->GetElementById("ready-state")) {
        ready->SetClass("visible", snapshot_.phase == PlayLevelUiPhase::WaitingToStart);
        if (snapshot_.phase == PlayLevelUiPhase::WaitingToStart) {
            ready->SetInnerRML(session_.isClient() ? "WAITING FOR HOST TO START" : "WORLD READY");
        }
    }
    if (Rml::Element* role = document_->GetElementById("match-role")) {
        role->SetInnerRML(!onlineMatch_ ? "SOLO" : (session_.isHost() ? "HOST" : "CLIENT"));
    }
    if (Rml::Element* start = document_->GetElementById("start-match-button")) {
        const bool visible = snapshot_.phase == PlayLevelUiPhase::WaitingToStart && !session_.isClient();
        start->SetClass("visible", visible);
        if (visible && onlineMatch_ && !session_.allMembersLoadedReady()) {
            start->SetAttribute("disabled", "");
        } else {
            start->RemoveAttribute("disabled");
        }
    }
}

} // namespace NodeSpireUi
