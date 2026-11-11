#pragma once

#include "HIKARI/Project/GameSystem.h"

namespace Project_AL4_PJ2 {

// PlayerComponent を持つオブジェクトを入力に従って動かす：移動・照準・射撃・リロード・回避と、
// それに合わせたアニメーションの切り替え、弾の表示、見下ろしカメラの追従。
class PlayerSystem final : public HIKARI::IGameSystem {
public:
    void Update(HIKARI::WorldView& world, const HIKARI::FrameContext& frame) override;
};

} // namespace Project_AL4_PJ2
