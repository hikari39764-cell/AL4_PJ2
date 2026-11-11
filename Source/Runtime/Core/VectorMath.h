#pragma once

#include <cmath>

#include "HIKARI/Project/ValueTypes.h"

namespace Project_AL4_PJ2 {

using Float3 = HIKARI::GameProjectFloat3;

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kDegreesToRadians = kPi / 180.0f;

inline Float3 Add(Float3 a, Float3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

inline Float3 Subtract(Float3 a, Float3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

inline Float3 Scale(Float3 value, float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

inline float Dot(Float3 a, Float3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Float3 Cross(Float3 a, Float3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float Length(Float3 value) noexcept { return std::sqrt(Dot(value, value)); }

// 長さがほぼ 0 のときは fallback を返す（向きが決まらない入力で NaN を出さないため）。
inline Float3 NormalizeOr(Float3 value, Float3 fallback) noexcept {
    const float length = Length(value);
    return length > 1.0e-5f ? Scale(value, 1.0f / length) : fallback;
}

// 水平の向き（+Z が 0、+X が +90 度）と、その逆。
inline Float3 DirectionFromYaw(float yawRadians) noexcept { return {std::sin(yawRadians), 0.0f, std::cos(yawRadians)}; }

inline float YawFromDirection(Float3 direction) noexcept { return std::atan2(direction.x, direction.z); }

inline HIKARI::GameProjectQuaternion YawRotation(float yawRadians) noexcept {
    const float half = yawRadians * 0.5f;
    return {0.0f, std::sin(half), 0.0f, std::cos(half)};
}

// 回転が前方（+Z）を水平面のどこへ向けるか。
inline float YawFromRotation(HIKARI::GameProjectQuaternion rotation) noexcept {
    const float forwardX = 2.0f * (rotation.x * rotation.z + rotation.w * rotation.y);
    const float forwardZ = 1.0f - 2.0f * (rotation.x * rotation.x + rotation.y * rotation.y);
    return std::atan2(forwardX, forwardZ);
}

// axis は正規化済みであること。
inline HIKARI::GameProjectQuaternion AxisAngleRotation(Float3 axis, float radians) noexcept {
    const float half = radians * 0.5f;
    const float sine = std::sin(half);
    return {axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half)};
}

// second を先に、first を後に掛けた回転（first ⊗ second）。
inline HIKARI::GameProjectQuaternion Multiply(HIKARI::GameProjectQuaternion first,
                                              HIKARI::GameProjectQuaternion second) noexcept {
    return {first.w * second.x + first.x * second.w + first.y * second.z - first.z * second.y,
            first.w * second.y - first.x * second.z + first.y * second.w + first.z * second.x,
            first.w * second.z + first.x * second.y - first.y * second.x + first.z * second.w,
            first.w * second.w - first.x * second.x - first.y * second.y - first.z * second.z};
}

inline Float3 Rotate(HIKARI::GameProjectQuaternion rotation, Float3 value) noexcept {
    const Float3 axis{rotation.x, rotation.y, rotation.z};
    const Float3 twice = Scale(Cross(axis, value), 2.0f);
    return Add(value, Add(Scale(twice, rotation.w), Cross(axis, twice)));
}

inline float WrapAngle(float radians) noexcept {
    const float wrapped = std::fmod(radians + kPi, 2.0f * kPi);
    return (wrapped < 0.0f ? wrapped + 2.0f * kPi : wrapped) - kPi;
}

// current を target へ、近い回り方で最大 maxStep だけ近づける。
inline float ApproachAngle(float current, float target, float maxStep) noexcept {
    const float delta = WrapAngle(target - current);
    if (std::fabs(delta) <= maxStep) {
        return target;
    }
    return current + (delta > 0.0f ? maxStep : -maxStep);
}

} // namespace Project_AL4_PJ2
