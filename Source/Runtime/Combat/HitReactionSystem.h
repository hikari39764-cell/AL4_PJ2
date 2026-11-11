#pragma once

#include <vector>

#include "HIKARI/Project/GameSystem.h"

#include "../Core/VectorMath.h"

namespace Project_AL4_PJ2 {

class HitTargetComponent;

struct HitInfo {
    Float3 position{};
    // 弾が飛んできた向き（水平、正規化済み）。
    Float3 direction{0.0f, 0.0f, 1.0f};
    float damage = 1.0f;
};

// 撃った側が自分で掛ける演出の強さ（時間を止める長さと、カメラの揺れ）。的ごとに重さが違うので的が決める。
struct HitFeedback {
    float hitStopSeconds = 0.0f;
    float hitStopScale = 1.0f;
    float cameraShake = 0.0f;
};

// 弾との当たりを見るために集めた的 1 つ分。そのフレームの中でだけ使う。
struct HitTargetEntry {
    HIKARI::ObjectView object{};
    HitTargetComponent* component = nullptr;
    Float3 position{};
};

// World にある的を全部 outTargets に入れ直す（前の中身は捨てる。毎フレーム確保し直さないよう、同じ配列を使い回す）。
void CollectHitTargets(const HIKARI::WorldView& world, std::vector<HitTargetEntry>& outTargets);

// 線分 from → to が的の当たり判定に入る位置を、線分上の割合（0〜1）で返す。当たらなければ false。
// 倒れている的には当たらない。
bool IntersectHurtVolume(const HitTargetComponent& target, Float3 targetPosition, Float3 from, Float3 to,
                         float& outFraction) noexcept;

// 当たりを的に反映する（耐久を減らし、のけぞり・つぶれ・発光・効果を始める）。撃った側へ返す手応えを返す。
HitFeedback ApplyHit(const HIKARI::WorldView& world, HIKARI::ObjectView object, HitTargetComponent& target,
                     const HitInfo& hit);

// HitTargetComponent の手応えを毎フレーム進める：ばねで戻る傾き、つぶれ、発光、倒れてからの復帰。
class HitReactionSystem final : public HIKARI::IGameSystem {
public:
    void Update(HIKARI::WorldView& world, const HIKARI::FrameContext& frame) override;
};

} // namespace Project_AL4_PJ2
