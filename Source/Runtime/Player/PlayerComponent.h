#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/VfxTypes.h"

namespace Project_AL4_PJ2 {

// シーンに保存される識別子。あとから変えない。
inline constexpr std::string_view kPlayerComponentType = "Project_AL4_PJ2.Player";
inline constexpr std::string_view kPlayerSystemId = "Project_AL4_PJ2.PlayerSystem";

// 同時に飛んでいられる弾の上限（射程 ÷ 弾速 × 連射数より十分大きい容量。調整値ではない）。
inline constexpr size_t kPlayerBulletCapacity = 48u;

enum class PlayerAction : uint8_t {
    Locomotion,
    Dodge,
};

enum class PlayerBaseClip : uint8_t {
    None,
    Idle,
    Walk,
    Run,
    Dodge,
};

enum class PlayerUpperLayer : uint8_t {
    None,
    Fire,
    Reload,
};

struct PlayerBullet {
    HIKARI::GameProjectFloat3 position{};
    HIKARI::GameProjectFloat3 direction{0.0f, 0.0f, 1.0f};
    float remainingRange = 0.0f;
    bool active = false;
    // 弾と一緒に飛んでいる曳光の効果。
    HIKARI::GameProjectVfxPlaybackHandle effect{};
};

// Play 中だけの状態。シーンには保存しない。
struct PlayerRuntimeState {
    bool initialized = false;
    PlayerAction action = PlayerAction::Locomotion;
    PlayerBaseClip baseClip = PlayerBaseClip::None;
    PlayerUpperLayer upperLayer = PlayerUpperLayer::None;
    float yawRadians = 0.0f;
    float playbackSpeed = 0.0f;
    HIKARI::GameProjectFloat3 dodgeDirection{0.0f, 0.0f, 1.0f};
    float dodgeElapsed = 0.0f;
    float dodgeCooldown = 0.0f;
    float fireCooldown = 0.0f;
    // 最後に撃ってからの秒数。負は「まだ撃っていない」。
    float sinceShot = -1.0f;
    // 負はリロード中でない。
    float reloadElapsed = -1.0f;
    int ammo = 0;
    uint32_t spreadSeed = 0x9E3779B9u;
    HIKARI::GameProjectFloat3 cameraFocus{};
    std::array<PlayerBullet, kPlayerBulletCapacity> bullets{};
};

// プレイヤー（Q）の調整値と Play 中の状態。規則は PlayerSystem が持つ。
class PlayerComponent final : public HIKARI::IGameComponent {
public:
    void DescribeProperties(HIKARI::PropertyFields& fields) override;

    // 移動
    float moveSpeed = 4.2f;
    float turnSpeedDegrees = 900.0f;
    float modelYawOffsetDegrees = 0.0f;

    // 回避
    float dodgeDistance = 4.0f;
    float dodgeSeconds = 0.5f;
    float dodgeCooldownSeconds = 0.35f;
    float dodgeFrontLoad = 1.6f;
    std::string dodgeVfxGuid;

    // 射撃
    float fireIntervalSeconds = 0.12f;
    int magazineSize = 24;
    float reloadSeconds = 1.58f;
    float spreadDegrees = 1.5f;
    float bulletSpeed = 28.0f;
    float bulletRange = 22.0f;
    float muzzleForward = 1.05f;
    float muzzleHeight = 1.45f;
    std::string muzzleVfxGuid;
    std::string bulletVfxGuid;

    // カメラ
    uint64_t cameraObjectId = 0u;
    float cameraPitchDegrees = 55.0f;
    float cameraYawDegrees = 0.0f;
    float cameraDistance = 16.0f;
    float cameraFovDegrees = 20.0f;
    float cameraFocusHeight = 1.0f;
    float cameraLookAhead = 1.2f;
    float cameraFollowSeconds = 0.12f;

    // 入力
    std::string moveAction = "Gameplay.Move";
    std::string aimAction = "Gameplay.Look";
    std::string fireAction = "Gameplay.Fire";
    std::string dodgeAction = "Gameplay.Dodge";
    std::string reloadAction = "Gameplay.Reload";
    bool firesWithPointerButton = true;

    // アニメーション
    std::string idleClip = "Idle";
    std::string walkClip = "Walk";
    std::string runClip = "Run";
    std::string dodgeClip = "Dodge";
    std::string fireClip = "Fire";
    std::string reloadClip = "Reload";
    std::string upperBodyNode = "chest";
    float walkClipSpeed = 1.46f;
    float runClipSpeed = 4.02f;
    float runSpeedThreshold = 2.2f;
    float dodgeClipSeconds = 0.5f;
    float fireClipSeconds = 0.208f;
    float blendSeconds = 0.12f;
    float layerBlendSeconds = 0.05f;

    PlayerRuntimeState state{};
};

} // namespace Project_AL4_PJ2
