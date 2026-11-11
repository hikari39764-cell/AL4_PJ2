#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "HIKARI/Project/GameComponent.h"

#include "../Core/VectorMath.h"

namespace Project_AL4_PJ2 {

// シーンに保存される識別子。あとから変えない。
inline constexpr std::string_view kHitTargetComponentType = "Project_AL4_PJ2.HitTarget";
inline constexpr std::string_view kHitReactionSystemId = "Project_AL4_PJ2.HitReactionSystem";

// Play 中だけの状態。シーンには保存しない。
struct HitTargetRuntimeState {
    bool initialized = false;
    // 揺らす前の姿勢。揺れはここからの差として毎フレーム書く。
    HIKARI::GameProjectTransform restTransform{};
    float health = 0.0f;
    // 傾き（ラジアン）。x・z が倒れている水平の向きで、長さが角度。
    Float3 lean{};
    Float3 leanVelocity{};
    // 当たった瞬間が 1 で、0 へ戻る（つぶれの強さ）。
    float squash = 0.0f;
    float flashRemaining = 0.0f;
    // 0 より大きい間は倒れている。
    float downRemaining = 0.0f;
    Float3 downDirection{0.0f, 0.0f, 1.0f};
    uint32_t effectSeed = 0x2545F491u;
};

// 弾が当たる的（サンドバッグや敵）。当たり判定の大きさ・耐久・当たったときの手応えの調整値を持つ。
// 規則は HitReactionSystem が持つ。
class HitTargetComponent final : public HIKARI::IGameComponent {
public:
    void DescribeProperties(HIKARI::PropertyFields& fields) override;

    // 当たり判定（オブジェクトの位置を軸にした縦の円柱）
    float hurtRadius = 0.42f;
    float hurtBottom = -0.55f;
    float hurtHeight = 2.0f;

    // 耐久
    float maxHealth = 14.0f;
    float downSeconds = 1.4f;

    // のけぞり（ばねで戻る傾き）
    float hitLeanDegreesPerSecond = 300.0f;
    float leanStiffness = 150.0f;
    float leanDamping = 9.0f;
    float maxLeanDegrees = 32.0f;
    float downLeanDegrees = 98.0f;

    // つぶれ
    float squashAmount = 0.14f;
    float squashSeconds = 0.12f;

    // 白く光る（当たった直後だけ、光る版のモデルを同じ姿勢で重ねて描く）
    std::string flashModelGuid;
    float flashSeconds = 0.06f;

    // 撃った側へ返す手応え
    float hitStopSeconds = 0.03f;
    float hitStopScale = 0.05f;
    float hitShake = 0.2f;
    float breakHitStopSeconds = 0.1f;
    float breakShake = 0.7f;

    // 効果
    std::string hitVfxGuid;
    std::string breakVfxGuid;
    float breakEffectHeight = 0.5f;

    HitTargetRuntimeState state{};
};

} // namespace Project_AL4_PJ2
