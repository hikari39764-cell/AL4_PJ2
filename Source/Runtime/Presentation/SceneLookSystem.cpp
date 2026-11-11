#include "SceneLookSystem.h"

#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/EnvironmentView.h"

namespace Project_AL4_PJ2 {
namespace {

// エンジン内蔵のピクセル化効果と、その値の置き場所（Pixel.hposteffect の slot / channel）。
constexpr std::string_view kPixelEffectId = "engine.post.effect.pixel";
constexpr uint32_t kPixelResolveSlot = 0u; // channel 1: 画素の大きさ、2: 色の段数、3: ディザ
constexpr uint32_t kPixelResolveMask = 0b1110u;
constexpr uint32_t kPixelOutlineSlot = 1u; // channel 0: 輪郭
constexpr uint32_t kPixelOutlineMask = 0b0001u;
// 上書きの提出元を見分ける番号。
constexpr uint64_t kPixelLookSourceId = 0x51504958u;

} // namespace

void SceneLookSystem::DescribeSettings(HIKARI::PropertyFields& fields) {
    fields.Text("renderStyle", "Render Style (engine.style.* or asset GUID)", renderStyleId_);
    if (fields.Section("Pixel Look", true)) {
        fields.Bool("pixelOverride", "Override Pixel Settings", overridesPixelLook_);
        fields.Float("pixelSize", "Pixel Size (1080p px)", pixelSize_, 1.0f, 16.0f, 0.05f);
        fields.Float("pixelColorLevels", "Color Levels", colorLevels_, 2.0f, 32.0f, 0.1f);
        fields.Float("pixelDither", "Dither", dither_, 0.0f, 1.0f, 0.01f);
        fields.Float("pixelOutline", "Outline", outline_, 0.0f, 1.0f, 0.01f);
    }
}

void SceneLookSystem::Update(HIKARI::WorldView& world, const HIKARI::FrameContext&) {
    // 編集中の画面は変えない。Play 中だけ。
    if (world.GetGameSession() == nullptr) {
        return;
    }
    ApplyRenderStyle(world);
    if (overridesPixelLook_) {
        SubmitPixelLook(world);
    }
}

// 様式の切り替えは一度通れば十分なので、受け付けられるまでだけ頼む。
void SceneLookSystem::ApplyRenderStyle(const HIKARI::WorldView& world) {
    if (renderStyleApplied_ || renderStyleId_.empty()) {
        return;
    }
    renderStyleApplied_ = world.GetEnvironment().SetRenderStyle(renderStyleId_).Accepted();
}

// 効果の値の上書きはそのフレームだけ有効なので、毎フレーム出す。
void SceneLookSystem::SubmitPixelLook(const HIKARI::WorldView& world) const {
    const HIKARI::PresentationVfxView presentation = world.GetPresentationVfx();
    HIKARI::GameProjectPostProfileParameterRequest request{};
    request.sourceId = kPixelLookSourceId;
    request.parameterIndex = kPixelResolveSlot;
    request.channelMask = kPixelResolveMask;
    request.value = {0.0f, pixelSize_, colorLevels_, dither_};
    presentation.SubmitPostProfileParameter(kPixelEffectId, request);

    request.parameterIndex = kPixelOutlineSlot;
    request.channelMask = kPixelOutlineMask;
    request.value = {outline_, 0.0f, 0.0f, 0.0f};
    presentation.SubmitPostProfileParameter(kPixelEffectId, request);
}

} // namespace Project_AL4_PJ2
