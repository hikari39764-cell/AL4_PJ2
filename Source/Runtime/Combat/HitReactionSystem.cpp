#include "HitReactionSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "HIKARI/Project/PresentationViews.h"
#include "HIKARI/Project/VfxView.h"

#include "../Core/EffectPlayback.h"
#include "HitTargetComponent.h"

namespace Project_AL4_PJ2 {
namespace {

constexpr Float3 kWorldUp{0.0f, 1.0f, 0.0f};
// ばねを 1 回で進める時間の上限。長いフレームでも発散しないように、これより長い分は分けて進める。
constexpr float kSpringStepSeconds = 1.0f / 120.0f;
constexpr int kMaximumSpringSteps = 8;
// 倒れている間の傾きの上限（度）。倒れる角度の調整値より大きく、ばねの行き過ぎで一回転しない値。
constexpr float kMaximumDownLeanDegrees = 125.0f;
// これより小さい傾きは回転なしとして扱う（軸が決まらないため）。
constexpr float kMinimumLeanRadians = 1.0e-4f;
// 重ねて描く光るモデルの番号（同じオブジェクトの中で重ならなければよい）。
constexpr uint64_t kFlashInstanceId = 0x464C5348u;

uint32_t NextSeed(uint32_t& seed) noexcept {
    seed ^= seed << 13u;
    seed ^= seed >> 17u;
    seed ^= seed << 5u;
    return seed;
}

void InitializeState(HitTargetComponent& target, const HIKARI::GameProjectTransform& transform) {
    target.state = HitTargetRuntimeState{};
    target.state.initialized = true;
    target.state.restTransform = transform;
    target.state.health = target.maxHealth;
}

// 傾きを、戻る先（立っていれば 0、倒れていれば倒れた向き）へ引くばねで進める。
void AdvanceLean(const HitTargetComponent& target, HitTargetRuntimeState& state, Float3 restLean, float dt) {
    const float angleBefore = Length(state.lean);
    const int steps = std::clamp(static_cast<int>(std::ceil(dt / kSpringStepSeconds)), 1, kMaximumSpringSteps);
    const float step = dt / static_cast<float>(steps);
    for (int index = 0; index < steps; ++index) {
        const Float3 acceleration = Subtract(Scale(Subtract(restLean, state.lean), target.leanStiffness),
                                             Scale(state.leanVelocity, target.leanDamping));
        state.leanVelocity = Add(state.leanVelocity, Scale(acceleration, step));
        state.lean = Add(state.lean, Scale(state.leanVelocity, step));
    }
    // 立っている間は倒れすぎないように止める。行き過ぎた分の勢いも捨てる。
    const float limit =
        (state.downRemaining > 0.0f ? kMaximumDownLeanDegrees : target.maxLeanDegrees) * kDegreesToRadians;
    const float angle = Length(state.lean);
    if (angle <= limit || angle <= 0.0f) {
        return;
    }
    const Float3 direction = Scale(state.lean, 1.0f / angle);
    const float outward = Dot(state.leanVelocity, direction);
    if (outward > 0.0f) {
        state.leanVelocity = Subtract(state.leanVelocity, Scale(direction, outward));
    }
    // 倒れた姿勢から起き上がる途中は上限より深いままなので、姿勢を上限へ飛ばさず、ばねで戻るのに任せる。
    if (angleBefore <= limit) {
        state.lean = Scale(direction, limit);
    }
}

HIKARI::GameProjectTransform PosedTransform(const HitTargetComponent& target, const HitTargetRuntimeState& state) {
    HIKARI::GameProjectTransform transform = state.restTransform;
    const float angle = Length(state.lean);
    if (angle > kMinimumLeanRadians) {
        // 上 × 倒れる向き を軸に回すと、上端が倒れる向きへ動く。
        const Float3 axis = Cross(kWorldUp, Scale(state.lean, 1.0f / angle));
        transform.rotation = Multiply(AxisAngleRotation(axis, angle), state.restTransform.rotation);
    }
    // 当たった瞬間に横へ太り、縦に縮む。2 乗で戻すと、戻り際がやわらかい。
    const float squash = target.squashAmount * state.squash * state.squash;
    transform.scale = {state.restTransform.scale.x * (1.0f + squash), state.restTransform.scale.y * (1.0f - squash),
                       state.restTransform.scale.z * (1.0f + squash)};
    return transform;
}

// 当たった直後だけ、光る版のモデルを的と同じ姿勢で重ねて描く。出したフレームにだけ表示されるので、
// 出すのをやめれば消える（的の材質には触らない）。
void UpdateFlash(const HIKARI::WorldView& world, HIKARI::ObjectView object, const HitTargetComponent& target,
                 HitTargetRuntimeState& state, const HIKARI::GameProjectTransform& transform, float dt) {
    if (state.flashRemaining <= 0.0f) {
        return;
    }
    state.flashRemaining -= dt;
    HIKARI::GameProjectPresentationModelRequest request{};
    if (target.flashModelGuid.empty() || target.flashModelGuid.size() >= sizeof(request.model.assetGuid)) {
        return;
    }
    std::memcpy(request.model.assetGuid, target.flashModelGuid.data(), target.flashModelGuid.size());
    request.ownerObjectHandle = object.GetHandle().value;
    request.instanceId = kFlashInstanceId;
    request.worldTransform = transform;
    request.castShadow = 0u;
    request.receiveShadow = 0u;
    world.GetPresentationModels().Submit(request);
}

void UpdateTarget(const HIKARI::WorldView& world, HIKARI::ObjectView object, HitTargetComponent& target, float dt) {
    HitTargetRuntimeState& state = target.state;
    if (!state.initialized) {
        HIKARI::GameProjectTransform transform{};
        if (!object.GetWorldTransform(transform)) {
            return;
        }
        InitializeState(target, transform);
    }
    Float3 restLean{};
    if (state.downRemaining > 0.0f) {
        state.downRemaining -= dt;
        if (state.downRemaining > 0.0f) {
            restLean = Scale(state.downDirection, target.downLeanDegrees * kDegreesToRadians);
        } else {
            // 起き上がるときに耐久を戻す。ばねが立った姿勢へ引き戻す。
            state.downRemaining = 0.0f;
            state.health = target.maxHealth;
        }
    }
    AdvanceLean(target, state, restLean, dt);
    state.squash = std::max(0.0f, state.squash - dt / std::max(target.squashSeconds, 1.0e-3f));
    const HIKARI::GameProjectTransform posed = PosedTransform(target, state);
    object.SetWorldTransform(posed);
    UpdateFlash(world, object, target, state, posed, dt);
}

} // namespace

void CollectHitTargets(const HIKARI::WorldView& world, std::vector<HitTargetEntry>& outTargets) {
    outTargets.clear();
    world.ForEachComponent<HitTargetComponent>(HIKARI::MakeComponentTypeId(kHitTargetComponentType),
                                               [&outTargets](HIKARI::ObjectView object, HitTargetComponent& target) {
                                                   HIKARI::GameProjectTransform transform{};
                                                   if (object.GetWorldTransform(transform)) {
                                                       outTargets.push_back({object, &target, transform.position});
                                                   }
                                               });
}

bool IntersectHurtVolume(const HitTargetComponent& target, Float3 targetPosition, Float3 from, Float3 to,
                         float& outFraction) noexcept {
    if (target.state.downRemaining > 0.0f) {
        return false;
    }
    // 水平面で円と線分の交点を求め、その高さが円柱の中かを見る。
    const float deltaX = to.x - from.x;
    const float deltaZ = to.z - from.z;
    const float offsetX = from.x - targetPosition.x;
    const float offsetZ = from.z - targetPosition.z;
    const float a = deltaX * deltaX + deltaZ * deltaZ;
    const float c = offsetX * offsetX + offsetZ * offsetZ - target.hurtRadius * target.hurtRadius;
    float fraction = 0.0f;
    if (c > 0.0f) {
        if (a <= 0.0f) {
            return false;
        }
        const float b = offsetX * deltaX + offsetZ * deltaZ;
        const float discriminant = b * b - a * c;
        if (b >= 0.0f || discriminant < 0.0f) {
            return false;
        }
        fraction = (-b - std::sqrt(discriminant)) / a;
        if (fraction > 1.0f) {
            return false;
        }
    }
    const float height = from.y + (to.y - from.y) * fraction - (targetPosition.y + target.hurtBottom);
    if (height < 0.0f || height > target.hurtHeight) {
        return false;
    }
    outFraction = fraction;
    return true;
}

HitFeedback ApplyHit(const HIKARI::WorldView& world, HIKARI::ObjectView object, HitTargetComponent& target,
                     const HitInfo& hit) {
    HitTargetRuntimeState& state = target.state;
    if (!state.initialized) {
        HIKARI::GameProjectTransform transform{};
        if (!object.GetWorldTransform(transform)) {
            return {};
        }
        InitializeState(target, transform);
    }
    const Float3 direction = NormalizeOr({hit.direction.x, 0.0f, hit.direction.z}, {0.0f, 0.0f, 1.0f});
    state.leanVelocity = Add(state.leanVelocity, Scale(direction, target.hitLeanDegreesPerSecond * kDegreesToRadians));
    state.squash = 1.0f;
    state.flashRemaining = target.flashSeconds;
    state.health -= hit.damage;

    const HIKARI::VfxView vfx = world.GetVfx();
    const uint64_t handle = object.GetHandle().value;
    // 効果の +Z を撃った側へ向ける（火花が手前へ返る）。
    const HIKARI::GameProjectQuaternion towardShooter = YawRotation(YawFromDirection(Scale(direction, -1.0f)));
    PlayEffect(vfx, target.hitVfxGuid, handle, hit.position, towardShooter, NextSeed(state.effectSeed));
    if (state.health > 0.0f) {
        return {target.hitStopSeconds, target.hitStopScale, target.hitShake};
    }
    state.downRemaining = target.downSeconds;
    state.downDirection = direction;
    // 倒れるときの効果は、弾が抜けていく側（倒れる向き）へ +Z を向ける。
    const Float3 centre = Add(state.restTransform.position, {0.0f, target.breakEffectHeight, 0.0f});
    PlayEffect(vfx, target.breakVfxGuid, handle, centre, YawRotation(YawFromDirection(direction)),
               NextSeed(state.effectSeed));
    return {target.breakHitStopSeconds, target.hitStopScale, target.breakShake};
}

void HitReactionSystem::Update(HIKARI::WorldView& world, const HIKARI::FrameContext& frame) {
    // 編集中にも呼ばれるので、Play 中（セッションがある）で、止まっていないときだけ動かす。
    if (world.GetGameSession() == nullptr || frame.paused) {
        return;
    }
    const float dt = frame.gameDt;
    world.ForEachComponent<HitTargetComponent>(HIKARI::MakeComponentTypeId(kHitTargetComponentType),
                                               [&world, dt](HIKARI::ObjectView object, HitTargetComponent& target) {
                                                   UpdateTarget(world, object, target, dt);
                                               });
}

} // namespace Project_AL4_PJ2
