#include "HitTargetComponent.h"

namespace Project_AL4_PJ2 {

void HitTargetComponent::DescribeProperties(HIKARI::PropertyFields& fields) {
    if (fields.Section("Hurt Volume", true)) {
        fields.Float("hurtRadius", "Radius (m)", hurtRadius, 0.05f, 5.0f, 0.01f);
        fields.Float("hurtBottom", "Bottom Offset (m)", hurtBottom, -5.0f, 5.0f, 0.01f);
        fields.Float("hurtHeight", "Height (m)", hurtHeight, 0.05f, 10.0f, 0.01f);
    }
    if (fields.Section("Durability", true)) {
        fields.Float("maxHealth", "Health (hits)", maxHealth, 1.0f, 999.0f, 1.0f);
        fields.Float("downSeconds", "Down Time (s)", downSeconds, 0.0f, 10.0f, 0.05f);
    }
    if (fields.Section("Recoil", true)) {
        fields.Float("hitLean", "Lean Kick Per Hit (deg/s)", hitLeanDegreesPerSecond, 0.0f, 2000.0f, 5.0f);
        fields.Float("leanStiffness", "Spring Stiffness", leanStiffness, 1.0f, 1000.0f, 1.0f);
        fields.Float("leanDamping", "Spring Damping", leanDamping, 0.0f, 100.0f, 0.1f);
        fields.Float("maxLean", "Max Lean (deg)", maxLeanDegrees, 0.0f, 80.0f, 0.5f);
        fields.Float("downLean", "Lean When Down (deg)", downLeanDegrees, 0.0f, 120.0f, 0.5f);
        fields.Float("squashAmount", "Squash Amount", squashAmount, 0.0f, 0.6f, 0.01f);
        fields.Float("squashSeconds", "Squash Time (s)", squashSeconds, 0.01f, 1.0f, 0.01f);
    }
    if (fields.Section("Flash", true)) {
        fields.Asset("flashModel", "Flash Model (drawn over the body)", HIKARI::GameProjectAssetKind::Model,
                     flashModelGuid);
        fields.Float("flashSeconds", "Flash Time (s, 0 = off)", flashSeconds, 0.0f, 1.0f, 0.005f);
    }
    if (fields.Section("Feedback To Shooter", true)) {
        fields.Float("hitStopSeconds", "Hit Stop (s, 0 = off)", hitStopSeconds, 0.0f, 0.5f, 0.005f);
        fields.Float("hitStopScale", "Hit Stop Time Scale", hitStopScale, 0.0f, 1.0f, 0.01f);
        fields.Float("hitShake", "Camera Shake Per Hit", hitShake, 0.0f, 1.0f, 0.01f);
        fields.Float("breakHitStopSeconds", "Hit Stop On Down (s)", breakHitStopSeconds, 0.0f, 0.5f, 0.005f);
        fields.Float("breakShake", "Camera Shake On Down", breakShake, 0.0f, 1.0f, 0.01f);
    }
    if (fields.Section("Effects", true)) {
        fields.Asset("hitVfx", "Hit Effect", HIKARI::GameProjectAssetKind::VfxEffect, hitVfxGuid);
        fields.Asset("breakVfx", "Down Effect", HIKARI::GameProjectAssetKind::VfxEffect, breakVfxGuid);
        fields.Float("breakEffectHeight", "Down Effect Height (m)", breakEffectHeight, -5.0f, 5.0f, 0.01f);
    }
}

} // namespace Project_AL4_PJ2
