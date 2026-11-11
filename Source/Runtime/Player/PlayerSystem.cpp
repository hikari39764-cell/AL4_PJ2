#include "PlayerSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/PresentationViews.h"

#include "PlayerComponent.h"
#include "PlayerMath.h"

namespace Project_AL4_PJ2 {
namespace {

// スティックの遊び。入力機器の都合で決まる値で、見た目の調整値ではない。
constexpr float kStickDeadZone = 0.2f;
// これより遅ければ止まっているとみなし、待機のアニメーションにする。
constexpr float kMovingSpeedEpsilon = 0.05f;
// 再生倍率の変化がこれ未満なら設定し直さない（毎フレーム呼ばないため）。
constexpr float kPlaybackSpeedEpsilon = 0.01f;
// エンジンは範囲外の再生倍率を拒否するので、頼む前にこの範囲へ収める。
constexpr float kMinimumPlaybackSpeed = 0.1f;
constexpr float kMaximumPlaybackSpeed = 4.0f;
// ポインターがプレイヤーのほぼ真上にあると向きが暴れるため、これより近い狙いは使わない。
constexpr float kMinimumAimDistance = 0.15f;
// 真下を向くとカメラの上方向が決まらないので、俯角はここまで。
constexpr float kMaximumCameraPitchDegrees = 89.0f;
constexpr Float3 kWorldUp{0.0f, 1.0f, 0.0f};
// エンジンが提出元を見分けるための番号（同じオブジェクトの中で重ならなければよい）。
constexpr uint64_t kCameraSourceId = 0x514C504Cu;
constexpr uint64_t kMuzzleFlashInstanceId = 1u;
constexpr uint64_t kFirstBulletInstanceId = 2u;

struct PlayerInput {
    Float3 move{};
    Float3 aim{};
    bool hasAim = false;
    bool fireHeld = false;
    bool dodgePressed = false;
    bool reloadPressed = false;
};

struct BaseClipChoice {
    PlayerBaseClip clip = PlayerBaseClip::Idle;
    const std::string* name = nullptr;
    float playbackSpeed = 1.0f;
    bool loop = true;
};

bool ReadAction(const HIKARI::InputView& input, const std::string& actionId,
                HIKARI::GameProjectInputActionState& outState) {
    return !actionId.empty() && input.GetAction(actionId, outState);
}

// 画面上のポインターから地面（プレイヤーの足元の高さ）へ光線を引き、プレイヤーから見た向きを返す。
// 直近に表示されたフレームのカメラを使うので、そのフレームの画面とずれない。
bool PointerAim(const HIKARI::WorldView& world, Float3 playerPosition, Float3& outDirection) {
    HIKARI::GameProjectPointerState pointer{};
    HIKARI::GameProjectCameraViewSnapshot snapshot{};
    if (!world.GetInput().GetPointerState(pointer) || pointer.valid == 0u ||
        !world.GetCamera().TryGetViewSnapshot(snapshot)) {
        return false;
    }
    const HIKARI::GameProjectScreenRect& content = snapshot.sceneContentLogical;
    const float width = content.right - content.left;
    const float height = content.bottom - content.top;
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }
    const float screenX = (pointer.x - content.left) / width * 2.0f - 1.0f;
    const float screenY = 1.0f - (pointer.y - content.top) / height * 2.0f;
    const Float3 forward = NormalizeOr(Subtract(snapshot.pose.target, snapshot.pose.eye), {0.0f, 0.0f, 1.0f});
    const Float3 right = NormalizeOr(Cross(snapshot.pose.up, forward), {1.0f, 0.0f, 0.0f});
    const Float3 up = Cross(forward, right);
    Float3 origin = snapshot.pose.eye;
    Float3 direction = forward;
    if (snapshot.lens.projection == HIKARI::GameProjectCameraProjectionKind::Orthographic) {
        const float halfHeight = snapshot.lens.orthographicHeight * 0.5f;
        origin = Add(origin,
                     Add(Scale(right, screenX * halfHeight * snapshot.aspectRatio), Scale(up, screenY * halfHeight)));
    } else {
        const float halfTangent = std::tan(snapshot.lens.verticalFovRadians * 0.5f);
        direction = Add(
            forward, Add(Scale(right, screenX * halfTangent * snapshot.aspectRatio), Scale(up, screenY * halfTangent)));
    }
    if (std::fabs(direction.y) < 1.0e-5f) {
        return false;
    }
    const float distance = (playerPosition.y - origin.y) / direction.y;
    if (distance <= 0.0f) {
        return false;
    }
    const Float3 hit = Add(origin, Scale(direction, distance));
    const Float3 toHit{hit.x - playerPosition.x, 0.0f, hit.z - playerPosition.z};
    if (Length(toHit) < kMinimumAimDistance) {
        return false;
    }
    outDirection = NormalizeOr(toHit, {0.0f, 0.0f, 1.0f});
    return true;
}

// 入力をカメラの向きに合わせた世界の向きへ直す（画面の上がカメラの前方）。
PlayerInput ReadInput(const HIKARI::WorldView& world, const PlayerComponent& player, Float3 position) {
    PlayerInput result{};
    const HIKARI::InputView input = world.GetInput();
    const Float3 cameraForward = DirectionFromYaw(player.cameraYawDegrees * kDegreesToRadians);
    const Float3 cameraRight = Cross(kWorldUp, cameraForward);

    HIKARI::GameProjectInputActionState action{};
    if (ReadAction(input, player.moveAction, action)) {
        const Float3 move = Add(Scale(cameraRight, action.x), Scale(cameraForward, action.y));
        const float length = Length(move);
        if (length > kStickDeadZone) {
            result.move = length > 1.0f ? Scale(move, 1.0f / length) : move;
        }
    }
    // Look にはマウスの移動量も割り当てられているので、向きとして使うのはゲームパッドのときだけ。
    if (input.GetLastActiveDevice() == HIKARI::GameProjectInputDevice::Gamepad) {
        if (ReadAction(input, player.aimAction, action)) {
            const Float3 aim = Add(Scale(cameraRight, action.x), Scale(cameraForward, action.y));
            if (Length(aim) > kStickDeadZone) {
                result.aim = NormalizeOr(aim, cameraForward);
                result.hasAim = true;
            }
        }
    } else {
        result.hasAim = PointerAim(world, position, result.aim);
    }
    if (ReadAction(input, player.fireAction, action)) {
        result.fireHeld = action.down != 0u;
    }
    HIKARI::GameProjectPointerState pointer{};
    if (player.firesWithPointerButton && input.GetPointerState(pointer) && pointer.valid != 0u &&
        pointer.primaryDown != 0u) {
        result.fireHeld = true;
    }
    if (ReadAction(input, player.dodgeAction, action)) {
        result.dodgePressed = action.pressed != 0u;
    }
    if (ReadAction(input, player.reloadAction, action)) {
        result.reloadPressed = action.pressed != 0u;
    }
    return result;
}

void InitializeState(PlayerComponent& player, const HIKARI::GameProjectTransform& transform) {
    player.state = PlayerRuntimeState{};
    player.state.initialized = true;
    player.state.yawRadians = YawFromRotation(transform.rotation) - player.modelYawOffsetDegrees * kDegreesToRadians;
    player.state.ammo = player.magazineSize;
    player.state.cameraFocus = transform.position;
}

void AdvanceTimers(const PlayerComponent& player, PlayerRuntimeState& state, float dt) {
    state.fireCooldown = std::max(0.0f, state.fireCooldown - dt);
    state.dodgeCooldown = std::max(0.0f, state.dodgeCooldown - dt);
    if (state.sinceShot >= 0.0f) {
        state.sinceShot += dt;
    }
    if (state.reloadElapsed >= 0.0f) {
        state.reloadElapsed += dt;
        if (state.reloadElapsed >= player.reloadSeconds) {
            state.ammo = player.magazineSize;
            state.reloadElapsed = -1.0f;
        }
    }
}

// このフレームの速度を返す。回避中は入力を受け付けず、決めた向きへ一定の速さで進む。
Float3 UpdateMovement(const PlayerComponent& player, PlayerRuntimeState& state, const PlayerInput& input, float dt) {
    if (state.action == PlayerAction::Locomotion && input.dodgePressed && state.dodgeCooldown <= 0.0f) {
        state.action = PlayerAction::Dodge;
        state.dodgeElapsed = 0.0f;
        state.dodgeDirection = NormalizeOr(input.move, DirectionFromYaw(state.yawRadians));
    }
    if (state.action == PlayerAction::Dodge) {
        state.dodgeElapsed += dt;
        if (state.dodgeElapsed < player.dodgeSeconds) {
            return Scale(state.dodgeDirection, player.dodgeDistance / player.dodgeSeconds);
        }
        state.action = PlayerAction::Locomotion;
        state.dodgeCooldown = player.dodgeCooldownSeconds;
    }
    return Scale(input.move, player.moveSpeed);
}

// 向きの優先順：回避の向き → 狙っている向き → 進んでいる向き。
void UpdateFacing(const PlayerComponent& player, PlayerRuntimeState& state, const PlayerInput& input, Float3 velocity,
                  float dt) {
    float targetYaw = state.yawRadians;
    if (state.action == PlayerAction::Dodge) {
        targetYaw = YawFromDirection(state.dodgeDirection);
    } else if (input.hasAim) {
        targetYaw = YawFromDirection(input.aim);
    } else if (Length(velocity) > kMovingSpeedEpsilon) {
        targetYaw = YawFromDirection(velocity);
    }
    state.yawRadians = ApproachAngle(state.yawRadians, targetYaw, player.turnSpeedDegrees * kDegreesToRadians * dt);
}

// xorshift32。弾のばらつき用で、プレイヤーごとの種だけを進める（共有の乱数状態を持たない）。
float NextUnitRandom(uint32_t& seed) noexcept {
    seed ^= seed << 13u;
    seed ^= seed >> 17u;
    seed ^= seed << 5u;
    return static_cast<float>(seed >> 8u) * (1.0f / 16777216.0f);
}

Float3 MuzzlePosition(const PlayerComponent& player, Float3 position, float yawRadians) {
    const Float3 forward = Scale(DirectionFromYaw(yawRadians), player.muzzleForward);
    return Add(Add(position, forward), {0.0f, player.muzzleHeight, 0.0f});
}

void UpdateWeapon(const PlayerComponent& player, PlayerRuntimeState& state, const PlayerInput& input, Float3 position) {
    const bool reloading = state.reloadElapsed >= 0.0f;
    if (!reloading && input.reloadPressed && state.ammo < player.magazineSize) {
        state.reloadElapsed = 0.0f;
        return;
    }
    if (reloading || state.action != PlayerAction::Locomotion || !input.fireHeld) {
        return;
    }
    if (state.ammo <= 0) {
        // 弾切れのまま撃とうとしたら、押し直さなくてもリロードが始まるようにする。
        state.reloadElapsed = 0.0f;
        return;
    }
    if (state.fireCooldown > 0.0f) {
        return;
    }
    const auto slot = std::find_if(state.bullets.begin(), state.bullets.end(),
                                   [](const PlayerBullet& bullet) { return !bullet.active; });
    if (slot == state.bullets.end()) {
        return;
    }
    const float spread = (NextUnitRandom(state.spreadSeed) * 2.0f - 1.0f) * player.spreadDegrees * kDegreesToRadians;
    slot->position = MuzzlePosition(player, position, state.yawRadians);
    slot->direction = DirectionFromYaw(state.yawRadians + spread);
    slot->remainingRange = player.bulletRange;
    slot->active = true;
    --state.ammo;
    state.fireCooldown = player.fireIntervalSeconds;
    state.sinceShot = 0.0f;
}

// 描画だけのモデルを 1 フレーム分頼む。次のフレームに頼まなければ自動で消える。
void SubmitModel(const HIKARI::PresentationModelView& models, const std::string& modelGuid, uint64_t ownerHandle,
                 uint64_t instanceId, Float3 position, float yawRadians, float scale) {
    HIKARI::GameProjectPresentationModelRequest request{};
    if (modelGuid.empty() || modelGuid.size() >= sizeof(request.model.assetGuid)) {
        return;
    }
    std::memcpy(request.model.assetGuid, modelGuid.data(), modelGuid.size());
    request.ownerObjectHandle = ownerHandle;
    request.instanceId = instanceId;
    request.worldTransform.position = position;
    request.worldTransform.rotation = YawRotation(yawRadians);
    request.worldTransform.scale = {scale, scale, scale};
    request.castShadow = 0u;
    request.receiveShadow = 0u;
    models.Submit(request);
}

void UpdateBullets(const HIKARI::WorldView& world, uint64_t ownerHandle, const PlayerComponent& player,
                   PlayerRuntimeState& state, Float3 position, float dt) {
    const HIKARI::PresentationModelView models = world.GetPresentationModels();
    const float step = player.bulletSpeed * dt;
    for (size_t index = 0u; index < state.bullets.size(); ++index) {
        PlayerBullet& bullet = state.bullets[index];
        if (!bullet.active) {
            continue;
        }
        bullet.position = Add(bullet.position, Scale(bullet.direction, step));
        bullet.remainingRange -= step;
        if (bullet.remainingRange <= 0.0f) {
            bullet.active = false;
            continue;
        }
        SubmitModel(models, player.bulletModelGuid, ownerHandle, kFirstBulletInstanceId + index, bullet.position,
                    YawFromDirection(bullet.direction), player.bulletScale);
    }
    if (state.sinceShot >= 0.0f && state.sinceShot < player.muzzleFlashSeconds) {
        SubmitModel(models, player.bulletModelGuid, ownerHandle, kMuzzleFlashInstanceId,
                    MuzzlePosition(player, position, state.yawRadians), state.yawRadians,
                    player.bulletScale * player.muzzleFlashScale);
    }
}

// 足の動きが地面の速さと合うように、クリップを作ったときの速さとの比で再生倍率を決める。
BaseClipChoice ChooseBaseClip(const PlayerComponent& player, const PlayerRuntimeState& state, float speed) {
    if (state.action == PlayerAction::Dodge) {
        return {PlayerBaseClip::Dodge, &player.dodgeClip, player.dodgeClipSeconds / player.dodgeSeconds, false};
    }
    if (speed >= player.runSpeedThreshold) {
        return {PlayerBaseClip::Run, &player.runClip, speed / player.runClipSpeed, true};
    }
    if (speed > kMovingSpeedEpsilon) {
        return {PlayerBaseClip::Walk, &player.walkClip, speed / player.walkClipSpeed, true};
    }
    return {PlayerBaseClip::Idle, &player.idleClip, 1.0f, true};
}

// 射撃とリロードは上半身だけに重ねる。足は下のクリップ（待機・歩き・走り）のまま動き続ける。
void UpdateUpperLayer(const HIKARI::AnimationView& animation, uint64_t handle, const PlayerComponent& player,
                      PlayerRuntimeState& state) {
    PlayerUpperLayer layer = PlayerUpperLayer::None;
    const std::string* clip = nullptr;
    float normalizedTime = 0.0f;
    if (state.action == PlayerAction::Locomotion) {
        if (state.reloadElapsed >= 0.0f) {
            layer = PlayerUpperLayer::Reload;
            clip = &player.reloadClip;
            normalizedTime = state.reloadElapsed / player.reloadSeconds;
        } else if (state.sinceShot >= 0.0f && state.sinceShot < player.fireClipSeconds) {
            layer = PlayerUpperLayer::Fire;
            clip = &player.fireClip;
            normalizedTime = state.sinceShot / player.fireClipSeconds;
        }
    }
    if (clip != nullptr) {
        const float fullWeight = 1.0f;
        animation.SetMaskedPoseLayer(handle, *clip, player.upperBodyNode, std::clamp(normalizedTime, 0.0f, 1.0f),
                                     fullWeight, player.layerBlendSeconds);
    } else if (state.upperLayer != PlayerUpperLayer::None) {
        animation.ClearMaskedPoseLayer(handle, player.layerBlendSeconds);
    }
    state.upperLayer = layer;
}

void UpdateAnimation(const HIKARI::WorldView& world, uint64_t handle, const PlayerComponent& player,
                     PlayerRuntimeState& state, float speed) {
    const HIKARI::AnimationView animation = world.GetAnimation();
    const BaseClipChoice choice = ChooseBaseClip(player, state, speed);
    // Animator の準備ができる前は失敗する。成功したときだけ覚えて、失敗なら次のフレームにやり直す。
    if (choice.clip != state.baseClip &&
        animation.CrossFade(handle, *choice.name, player.blendSeconds, static_cast<uint8_t>(choice.loop))) {
        state.baseClip = choice.clip;
        state.playbackSpeed = 0.0f;
    }
    const float playbackSpeed = std::clamp(choice.playbackSpeed, kMinimumPlaybackSpeed, kMaximumPlaybackSpeed);
    if (state.baseClip == choice.clip && std::fabs(playbackSpeed - state.playbackSpeed) > kPlaybackSpeedEpsilon &&
        animation.SetSpeed(handle, playbackSpeed)) {
        state.playbackSpeed = playbackSpeed;
    }
    UpdateUpperLayer(animation, handle, player, state);
}

// 見下ろしカメラ。注視点を少し遅らせて追わせ、向いている側を少し広く見せる。
void UpdateCamera(const HIKARI::WorldView& world, const PlayerComponent& player, PlayerRuntimeState& state,
                  Float3 position, float dt) {
    if (player.cameraObjectId == 0u) {
        return;
    }
    const HIKARI::ObjectView cameraObject = world.FindSceneObject(player.cameraObjectId);
    if (!cameraObject.IsValid()) {
        return;
    }
    const Float3 focus = Add(position, Scale(DirectionFromYaw(state.yawRadians), player.cameraLookAhead));
    const float follow = player.cameraFollowSeconds > 0.0f ? 1.0f - std::exp(-dt / player.cameraFollowSeconds) : 1.0f;
    state.cameraFocus = Add(state.cameraFocus, Scale(Subtract(focus, state.cameraFocus), follow));

    const float pitch = std::min(player.cameraPitchDegrees, kMaximumCameraPitchDegrees) * kDegreesToRadians;
    const Float3 horizontal = DirectionFromYaw(player.cameraYawDegrees * kDegreesToRadians);
    const Float3 viewDirection{horizontal.x * std::cos(pitch), -std::sin(pitch), horizontal.z * std::cos(pitch)};

    const HIKARI::CameraView camera = world.GetCamera();
    HIKARI::GameProjectCameraPoseRequest request{};
    request.cameraObjectHandle = cameraObject.GetHandle().value;
    request.sourceId = kCameraSourceId;
    request.pose.target = Add(state.cameraFocus, {0.0f, player.cameraFocusHeight, 0.0f});
    request.pose.eye = Subtract(request.pose.target, Scale(viewDirection, player.cameraDistance));
    request.pose.up = kWorldUp;
    request.pose.verticalFovRadians = player.cameraFovDegrees * kDegreesToRadians;
    HIKARI::GameProjectCameraPose current{};
    if (camera.GetControlPose(current)) {
        request.pose.aspectRatio = current.aspectRatio;
    }
    camera.SubmitPose(request);
}

void UpdatePlayer(const HIKARI::WorldView& world, HIKARI::ObjectView object, PlayerComponent& player, float dt) {
    HIKARI::GameProjectTransform transform{};
    if (!object.GetWorldTransform(transform)) {
        return;
    }
    PlayerRuntimeState& state = player.state;
    if (!state.initialized) {
        InitializeState(player, transform);
    }
    const PlayerInput input = ReadInput(world, player, transform.position);
    AdvanceTimers(player, state, dt);
    const Float3 velocity = UpdateMovement(player, state, input, dt);
    UpdateFacing(player, state, input, velocity, dt);

    transform.position = Add(transform.position, Scale(velocity, dt));
    transform.rotation = YawRotation(state.yawRadians + player.modelYawOffsetDegrees * kDegreesToRadians);
    if (!object.SetWorldTransform(transform).Accepted()) {
        return;
    }
    const uint64_t handle = object.GetHandle().value;
    UpdateWeapon(player, state, input, transform.position);
    UpdateAnimation(world, handle, player, state, Length(velocity));
    UpdateBullets(world, handle, player, state, transform.position, dt);
    UpdateCamera(world, player, state, transform.position, dt);
}

} // namespace

void PlayerSystem::Update(HIKARI::WorldView& world, const HIKARI::FrameContext& frame) {
    // 編集中にも呼ばれるので、Play 中（セッションがある）で、止まっていないときだけ動かす。
    if (world.GetGameSession() == nullptr || frame.paused) {
        return;
    }
    const float dt = frame.gameDt;
    world.ForEachComponent<PlayerComponent>(
        HIKARI::MakeComponentTypeId(kPlayerComponentType),
        [&world, dt](HIKARI::ObjectView object, PlayerComponent& player) { UpdatePlayer(world, object, player, dt); });
}

} // namespace Project_AL4_PJ2
