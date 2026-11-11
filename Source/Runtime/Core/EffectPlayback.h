#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "HIKARI/Project/VfxView.h"

#include "VectorMath.h"

namespace Project_AL4_PJ2 {

// 効果を 1 回再生する。資産が設定されていなければ何もしない（無効なハンドルを返す）。
// コンポーネントは資産を GUID の文字列で持つので、要求の固定長の欄へ写してから渡す。
inline HIKARI::GameProjectVfxPlaybackHandle PlayEffect(const HIKARI::VfxView& vfx, const std::string& assetGuid,
                                                       uint64_t ownerHandle, Float3 position,
                                                       HIKARI::GameProjectQuaternion rotation, uint32_t randomSeed) {
    HIKARI::GameProjectVfxPlayRequest request{};
    if (assetGuid.empty() || assetGuid.size() >= sizeof(request.asset.assetGuid)) {
        return {};
    }
    std::memcpy(request.asset.assetGuid, assetGuid.data(), assetGuid.size());
    request.ownerObjectHandle = ownerHandle;
    request.transform.position = position;
    request.transform.rotation = rotation;
    request.randomSeed = randomSeed;
    return vfx.Play(request);
}

} // namespace Project_AL4_PJ2
