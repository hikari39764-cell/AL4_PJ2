#include "HIKARI/Project/GameFlowViews.h"
#include "HIKARI/Project/GameProjectModule.h"
#include <array>
#include <cstdint>
#include <memory>

#include "Player/PlayerComponent.h"
#include "Player/PlayerSystem.h"
#include "Presentation/SceneLookSystem.h"

namespace Project_AL4_PJ2 {

// Play 中にシーンをまたいで保持するプロジェクト状態。
class GameSession final : public HIKARI::IGameProjectSession {
public:
    void OnGameStarted(HIKARI::WorldView&) override {
        gameplaySeconds = 0.0f;
        enteredScenes = 0;
        sceneActive = false;
    }

    void OnSceneEntered(HIKARI::WorldView&) override {
        ++enteredScenes;
        sceneActive = true;
    }
    void OnSceneExiting(HIKARI::WorldView&) override { sceneActive = false; }

    float gameplaySeconds = 0.0f;
    uint32_t enteredScenes = 0;
    bool sceneActive = false;
};

// システムは通常どおり World に Attach され、ゲーム状態は Play session から取得する。
class Game final : public HIKARI::IGameSystem {
public:
    void Update(HIKARI::WorldView& world, const HIKARI::FrameContext& frame) override {
        auto* session = dynamic_cast<GameSession*>(world.GetGameSession());
        if (session == nullptr || !session->sceneActive || frame.paused) {
            return;
        }
        session->gameplaySeconds += frame.gameDt;
    }
};

// 以下はエンジンへの登録。日常のゲーム処理は上の Game に書く。
// Register で登録したコンポーネントとシステムは自動的にこの Feature の所有になる。
class GameplayFeature final : public HIKARI::IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return "Project_AL4_PJ2.Gameplay"; }
    std::string_view GetDisplayName() const noexcept override { return "Gameplay"; }
    std::string_view GetDescription() const noexcept override { return "Project game logic"; }
    // プレイヤーがアニメーションと射撃の VFX を使い、SceneLook が後処理の値を上書きするので、必ず有効にさせる。
    std::span<const std::string_view> GetRequiredFeatureIds() const noexcept override {
        static constexpr std::array<std::string_view, 3> features{{"Animation", "Vfx", "ActionPresentation"}};
        return features;
    }
    std::span<const HIKARI::RuntimeSystemDefault> GetDefaultSystems() const noexcept override {
        static constexpr std::array<HIKARI::RuntimeSystemDefault, 2> systems{
            {{"Project_AL4_PJ2.Game", true, 100}, {kSceneLookSystemId, true, 110}}};
        return systems;
    }
    bool Register(HIKARI::RuntimeFeatureContext& context) override {
        return context.RegisterSystem<Game>({.systemId = "Project_AL4_PJ2.Game", .displayName = "Game"}) &&
               context.RegisterComponent<PlayerComponent>({.typeName = kPlayerComponentType,
                                                           .displayName = "Player",
                                                           .description = "Q: move, aim, fire, reload, dodge"}) &&
               context.RegisterSystem<PlayerSystem>({.systemId = kPlayerSystemId, .displayName = "Player"}) &&
               context.RegisterSystem<SceneLookSystem>({.systemId = kSceneLookSystemId, .displayName = "Scene Look"}) &&
               context.BindComponentSystem({.componentType = kPlayerComponentType, .systemId = kPlayerSystemId});
    }
};

class GameProjectModule final : public HIKARI::RUNTIME::IGameProjectModule {
public:
    std::string_view GetModuleId() const noexcept override { return "Project_AL4_PJ2"; }
    bool RegisterRuntimeFeatures(HIKARI::RuntimeFeatureCatalog& catalog) override {
        return HIKARI::RegisterRuntimeFeature<GameplayFeature>(catalog);
    }
    std::unique_ptr<HIKARI::IGameProjectSession> CreateGameSession() override {
        return std::make_unique<GameSession>();
    }
};
} // namespace

namespace HIKARI::RUNTIME {
std::unique_ptr<IGameProjectModule> CreateGameProjectModule() {
    return std::make_unique<::Project_AL4_PJ2::GameProjectModule>();
}
} // namespace HIKARI::RUNTIME
