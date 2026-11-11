#include "PlayerSystem.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>

#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/VfxView.h"

#include "../Combat/HitTargetComponent.h"
#include "../Core/EffectPlayback.h"
#include "../Core/VectorMath.h"
#include "PlayerComponent.h"

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
constexpr uint64_t kHitStopSourceId = 0x51485354u;
// カメラの揺れは、周期の合わない 2 つの波を重ねて、規則的に見えないようにする。
// 値は波の形（周波数の比・位相・混ぜる割合）で、揺れの強さと速さはコンポーネントの調整値が決める。
constexpr float kShakeSecondWaveRatio = 2.3f;
constexpr float kShakeVerticalRatio = 1.3f;
constexpr float kShakeSecondWaveWeight = 0.4f;
constexpr float kShakePhaseA = 1.7f;
constexpr float kShakePhaseB = 0.9f;
constexpr float kShakePhaseC = 4.1f;

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
    const Float3 right = NormalizeOr(Cross(forward, snapshot.pose.up), {-1.0f, 0.0f, 0.0f});
    const Float3 up = Cross(right, forward);
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
    // エンジンは右手系なので、+Z を向くと画面の右は -X になる（前 × 上）。
    const Float3 cameraRight = Cross(cameraForward, kWorldUp);

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

// このフレームの速度を返す。回避中は入力を受け付けず、決めた向きへ進む。
// 回避の速さは出だしが速く終わりが緩い（dodgeFrontLoad が 0 なら一定）。重みの積分は 1 なので、進む距離は変わらない。
Float3 UpdateMovement(const PlayerComponent& player, PlayerRuntimeState& state, const PlayerInput& input, float dt) {
    if (state.action == PlayerAction::Locomotion && input.dodgePressed && state.dodgeCooldown <= 0.0f) {
        state.action = PlayerAction::Dodge;
        state.dodgeElapsed = 0.0f;
        state.dodgeDirection = NormalizeOr(input.move, DirectionFromYaw(state.yawRadians));
    }
    if (state.action == PlayerAction::Dodge) {
        state.dodgeElapsed += dt;
        if (state.dodgeElapsed < player.dodgeSeconds) {
            // このフレームの中ほどの進み具合で速さを決める（端の値だと合計の距離がずれる）。
            const float progress = std::clamp((state.dodgeElapsed - 0.5f * dt) / player.dodgeSeconds, 0.0f, 1.0f);
            const float weight = (1.0f + player.dodgeFrontLoad) * std::pow(1.0f - progress, player.dodgeFrontLoad);
            return Scale(state.dodgeDirection, player.dodgeDistance / player.dodgeSeconds * weight);
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

// 銃口の位置。アニメーションで動いている銃の骨から取るので、弾と火花が銃の先から出る。
// 骨が取れないとき（名前が違う・Animator の準備前）は、体の向きから決めた位置を使う。
Float3 MuzzlePosition(const HIKARI::WorldView& world, uint64_t handle, const PlayerComponent& player, Float3 position,
                      float yawRadians) {
    HIKARI::GameProjectTransform socket{};
    if (!player.muzzleSocket.empty() &&
        world.GetAnimation().GetSocketTransform(handle, player.muzzleSocket,
                                                HIKARI::GameProjectAnimationSocketSpace::World, socket)) {
        const Float3 offset{player.muzzleSocketOffset.x * socket.scale.x, player.muzzleSocketOffset.y * socket.scale.y,
                            player.muzzleSocketOffset.z * socket.scale.z};
        return Add(socket.position, Rotate(socket.rotation, offset));
    }
    const Float3 forward = Scale(DirectionFromYaw(yawRadians), player.muzzleForward);
    return Add(Add(position, forward), {0.0f, player.muzzleHeight, 0.0f});
}

void AddCameraShake(PlayerRuntimeState& state, float amount) noexcept {
    state.cameraTrauma = std::min(1.0f, state.cameraTrauma + amount);
}

void UpdateWeapon(const HIKARI::WorldView& world, uint64_t ownerHandle, const PlayerComponent& player,
                  PlayerRuntimeState& state, const PlayerInput& input, Float3 position) {
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
    const float shotYaw = state.yawRadians + spread;
    const Float3 muzzle = MuzzlePosition(world, ownerHandle, player, position, state.yawRadians);
    const HIKARI::VfxView vfx = world.GetVfx();
    slot->position = muzzle;
    slot->direction = DirectionFromYaw(shotYaw);
    slot->remainingRange = player.bulletRange;
    slot->active = true;
    // 弾は効果の根を飛ばすだけ。粒子は通った跡に残って曳光になる。
    slot->effect = PlayEffect(vfx, player.bulletVfxGuid, ownerHandle, muzzle, YawRotation(shotYaw), state.spreadSeed);
    if (slot->effect.IsValid()) {
        vfx.SetVelocity(slot->effect, Scale(slot->direction, player.bulletSpeed));
    }
    PlayEffect(vfx, player.muzzleVfxGuid, ownerHandle, muzzle, YawRotation(state.yawRadians), state.spreadSeed);
    AddCameraShake(state, player.fireShake);
    --state.ammo;
    state.fireCooldown = player.fireIntervalSeconds;
    state.sinceShot = 0.0f;
}

void StopBullet(const HIKARI::VfxView& vfx, PlayerBullet& bullet) {
    // 曳光を止める。残っている粒子は自分の寿命で消える。
    if (bullet.effect.IsValid()) {
        vfx.Stop(bullet.effect);
    }
    bullet = PlayerBullet{};
}

// 当てた手応えを撃った側に掛ける：短く時間を止め、カメラを揺らす。
void ApplyHitFeedback(const HIKARI::WorldView& world, PlayerRuntimeState& state, const HitFeedback& feedback) {
    AddCameraShake(state, feedback.cameraShake);
    if (feedback.hitStopSeconds <= 0.0f) {
        return;
    }
    HIKARI::GameProjectTimeDilationRequest request{};
    request.sourceId = kHitStopSourceId;
    request.scale = std::clamp(feedback.hitStopScale, 0.0f, 1.0f);
    request.durationUnscaledSeconds = feedback.hitStopSeconds;
    world.GetTimeDilation().Request(request);
}

// 弾を進め、このフレームに通る線分が的に入ったら当てる。当たらずに射程が尽きた弾は消す。
void UpdateBullets(const HIKARI::WorldView& world, const PlayerComponent& player, PlayerRuntimeState& state,
                   std::span<const HitTargetEntry> targets, float dt) {
    const HIKARI::VfxView vfx = world.GetVfx();
    const float step = player.bulletSpeed * dt;
    // 同じフレームに何発か当たっても、時間を止めるのは一番長い 1 回分。揺れは足す。
    HitFeedback combined{};
    for (PlayerBullet& bullet : state.bullets) {
        if (!bullet.active) {
            continue;
        }
        const float travel = std::min(step, bullet.remainingRange);
        const Float3 end = Add(bullet.position, Scale(bullet.direction, travel));
        const HitTargetEntry* struck = nullptr;
        float nearest = 0.0f;
        for (const HitTargetEntry& entry : targets) {
            float fraction = 0.0f;
            if (IntersectHurtVolume(*entry.component, entry.position, bullet.position, end, fraction) &&
                (struck == nullptr || fraction < nearest)) {
                struck = &entry;
                nearest = fraction;
            }
        }
        if (struck != nullptr) {
            const Float3 point = Add(bullet.position, Scale(bullet.direction, travel * nearest));
            const HitFeedback feedback =
                ApplyHit(world, struck->object, *struck->component, {point, bullet.direction, player.bulletDamage});
            combined.cameraShake += feedback.cameraShake;
            if (feedback.hitStopSeconds > combined.hitStopSeconds) {
                combined.hitStopSeconds = feedback.hitStopSeconds;
                combined.hitStopScale = feedback.hitStopScale;
            }
            StopBullet(vfx, bullet);
            continue;
        }
        bullet.position = end;
        bullet.remainingRange -= travel;
        if (bullet.remainingRange <= 0.0f) {
            StopBullet(vfx, bullet);
        }
    }
    ApplyHitFeedback(world, state, combined);
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

// 煙を解く流体の域を置く。域は置いた場所から動かせない（動かすと、ほかの効果からの煙が入らなくなる）ので、
// プレイヤーが中心から離れたら置き直す。置き直すと中の煙は消えるため、しばらく撃っていないときを選ぶ。
// 域の端まで来てしまったら、待たずに置き直す。
void UpdateSmokeField(const HIKARI::VfxView& vfx, uint64_t ownerHandle, const PlayerComponent& player,
                      PlayerRuntimeState& state, Float3 position) {
    if (state.smokeField.IsValid()) {
        // 域は箱なので、軸ごとの離れ具合で見る。
        const Float3 offset = Subtract(position, state.smokeFieldCentre);
        const float distance = std::max(std::fabs(offset.x), std::fabs(offset.z));
        const bool quiet = state.sinceShot < 0.0f || state.sinceShot > player.smokeFieldQuietSeconds;
        const bool move =
            distance > player.smokeFieldMaximumDistance || (distance > player.smokeFieldRecenterDistance && quiet);
        if (!move) {
            return;
        }
        vfx.Stop(state.smokeField);
    }
    state.smokeField = PlayEffect(vfx, player.smokeFieldVfxGuid, ownerHandle, position, {}, state.spreadSeed);
    state.smokeFieldCentre = position;
}

// カメラの揺れを画面の面内のずれとして返す。時間が止まっている間も揺れるように、倍率の掛からない時間で進める。
Float3 AdvanceCameraShake(const PlayerComponent& player, PlayerRuntimeState& state, Float3 viewDirection,
                          float unscaledDt) {
    state.cameraTrauma = std::max(0.0f, state.cameraTrauma - player.cameraShakeDecay * unscaledDt);
    state.cameraShakeTime += unscaledDt;
    // 2 乗にすると、弱い揺れは目立たず、強い当たりだけがはっきり揺れる。
    const float strength = player.cameraShakeAmplitude * state.cameraTrauma * state.cameraTrauma;
    if (strength <= 0.0f) {
        return {};
    }
    const float phase = state.cameraShakeTime * player.cameraShakeFrequency * 2.0f * kPi;
    const float firstWeight = 1.0f - kShakeSecondWaveWeight;
    const float sideways =
        std::sin(phase) * firstWeight + std::sin(phase * kShakeSecondWaveRatio + kShakePhaseA) * kShakeSecondWaveWeight;
    const float vertical =
        std::sin(phase * kShakeVerticalRatio + kShakePhaseB) * firstWeight +
        std::sin(phase * kShakeVerticalRatio * kShakeSecondWaveRatio + kShakePhaseC) * kShakeSecondWaveWeight;
    const Float3 right = NormalizeOr(Cross(viewDirection, kWorldUp), {-1.0f, 0.0f, 0.0f});
    const Float3 up = Cross(right, viewDirection);
    return Scale(Add(Scale(right, sideways), Scale(up, vertical)), strength);
}

// 見下ろしカメラ。注視点を少し遅らせて追わせ、向いている側を少し広く見せる。
void UpdateCamera(const HIKARI::WorldView& world, const PlayerComponent& player, PlayerRuntimeState& state,
                  Float3 position, float dt, float unscaledDt) {
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
    const Float3 shake = AdvanceCameraShake(player, state, viewDirection, unscaledDt);

    const HIKARI::CameraView camera = world.GetCamera();
    HIKARI::GameProjectCameraPoseRequest request{};
    request.cameraObjectHandle = cameraObject.GetHandle().value;
    request.sourceId = kCameraSourceId;
    request.pose.target = Add(Add(state.cameraFocus, {0.0f, player.cameraFocusHeight, 0.0f}), shake);
    request.pose.eye = Subtract(request.pose.target, Scale(viewDirection, player.cameraDistance));
    request.pose.up = kWorldUp;
    request.pose.verticalFovRadians = player.cameraFovDegrees * kDegreesToRadians;
    HIKARI::GameProjectCameraPose current{};
    if (camera.GetControlPose(current)) {
        request.pose.aspectRatio = current.aspectRatio;
    }
    camera.SubmitPose(request);
}

void UpdatePlayer(const HIKARI::WorldView& world, HIKARI::ObjectView object, PlayerComponent& player,
                  std::span<const HitTargetEntry> targets, float dt, float unscaledDt) {
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
    const PlayerAction actionBefore = state.action;
    const Float3 velocity = UpdateMovement(player, state, input, dt);
    UpdateFacing(player, state, input, velocity, dt);

    transform.position = Add(transform.position, Scale(velocity, dt));
    transform.rotation = YawRotation(state.yawRadians + player.modelYawOffsetDegrees * kDegreesToRadians);
    if (!object.SetWorldTransform(transform).Accepted()) {
        return;
    }
    const uint64_t handle = object.GetHandle().value;
    const HIKARI::VfxView vfx = world.GetVfx();
    UpdateSmokeField(vfx, handle, player, state, transform.position);
    if (state.action == PlayerAction::Dodge && actionBefore != PlayerAction::Dodge) {
        // 踏み切った足元に土ぼこりを出す。
        PlayEffect(vfx, player.dodgeVfxGuid, handle, transform.position, YawRotation(state.yawRadians),
                   state.spreadSeed);
    }
    UpdateWeapon(world, handle, player, state, input, transform.position);
    UpdateAnimation(world, handle, player, state, Length(velocity));
    UpdateBullets(world, player, state, targets, dt);
    UpdateCamera(world, player, state, transform.position, dt, unscaledDt);
}

} // namespace

void PlayerSystem::Update(HIKARI::WorldView& world, const HIKARI::FrameContext& frame) {
    // 編集中にも呼ばれるので、Play 中（セッションがある）で、止まっていないときだけ動かす。
    if (world.GetGameSession() == nullptr || frame.paused) {
        return;
    }
    // 弾との当たりを見る的を、プレイヤーを回す前に集めておく（列挙の中で別の列挙をしないため）。
    CollectHitTargets(world, targets_);
    const float dt = frame.gameDt;
    const float unscaledDt = frame.unscaledDt;
    world.ForEachComponent<PlayerComponent>(
        HIKARI::MakeComponentTypeId(kPlayerComponentType),
        [this, &world, dt, unscaledDt](HIKARI::ObjectView object, PlayerComponent& player) {
            UpdatePlayer(world, object, player, targets_, dt, unscaledDt);
        });
}

} // namespace Project_AL4_PJ2
