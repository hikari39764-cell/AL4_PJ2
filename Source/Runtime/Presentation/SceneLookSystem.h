#pragma once

#include <string>
#include <string_view>

#include "HIKARI/Project/GameSystem.h"

namespace Project_AL4_PJ2 {

// シーンに保存される識別子。あとから変えない。
inline constexpr std::string_view kSceneLookSystemId = "Project_AL4_PJ2.SceneLook";

// シーンごとの画面の描き方：描画の様式（ピクセル化など）を選び、ピクセル化の細かさを上書きする。
// 値はシーンの「システム」設定に保存され、エディタで調整できる。
class SceneLookSystem final : public HIKARI::IGameSystem {
public:
    void DescribeSettings(HIKARI::PropertyFields& fields) override;
    void Update(HIKARI::WorldView& world, const HIKARI::FrameContext& frame) override;

private:
    void ApplyRenderStyle(const HIKARI::WorldView& world);
    void SubmitPixelLook(const HIKARI::WorldView& world) const;

    // 空ならシーンの様式のまま。内蔵の ID（engine.style.*）か、様式の資産の GUID。
    std::string renderStyleId_;
    bool renderStyleApplied_ = false;
    bool overridesPixelLook_ = false;
    float pixelSize_ = 4.0f;
    float colorLevels_ = 16.0f;
    float dither_ = 0.15f;
    float outline_ = 0.6f;
};

} // namespace Project_AL4_PJ2
