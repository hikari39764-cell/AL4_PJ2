#include "PlayerComponent.h"

namespace Project_AL4_PJ2 {

void PlayerComponent::DescribeProperties(HIKARI::PropertyFields& fields) {
    if (fields.Section("Movement", true)) {
        fields.Float("moveSpeed", "Move Speed (m/s)", moveSpeed, 0.0f, 12.0f, 0.05f);
        fields.Float("turnSpeed", "Turn Speed (deg/s)", turnSpeedDegrees, 0.0f, 2880.0f, 5.0f);
        fields.Float("modelYawOffset", "Model Yaw Offset (deg)", modelYawOffsetDegrees, -180.0f, 180.0f, 1.0f);
    }
    if (fields.Section("Dodge", true)) {
        fields.Float("dodgeDistance", "Distance (m)", dodgeDistance, 0.0f, 12.0f, 0.05f);
        fields.Float("dodgeSeconds", "Duration (s)", dodgeSeconds, 0.05f, 2.0f, 0.01f);
        fields.Float("dodgeCooldown", "Cooldown (s)", dodgeCooldownSeconds, 0.0f, 5.0f, 0.01f);
        fields.Float("dodgeFrontLoad", "Burst (0 = constant speed)", dodgeFrontLoad, 0.0f, 4.0f, 0.05f);
        fields.Asset("dodgeVfx", "Dodge Dust Effect", HIKARI::GameProjectAssetKind::VfxEffect, dodgeVfxGuid);
    }
    if (fields.Section("Weapon", true)) {
        fields.Float("fireInterval", "Fire Interval (s)", fireIntervalSeconds, 0.02f, 2.0f, 0.005f);
        fields.Int("magazineSize", "Magazine Size", magazineSize, 1, 999);
        fields.Float("reloadSeconds", "Reload Time (s)", reloadSeconds, 0.1f, 6.0f, 0.01f);
        fields.Float("spread", "Spread (deg)", spreadDegrees, 0.0f, 30.0f, 0.1f);
        fields.Float("bulletSpeed", "Bullet Speed (m/s)", bulletSpeed, 1.0f, 120.0f, 0.5f);
        fields.Float("bulletRange", "Bullet Range (m)", bulletRange, 1.0f, 100.0f, 0.5f);
        fields.Float("bulletDamage", "Bullet Damage", bulletDamage, 0.0f, 100.0f, 0.1f);
        fields.Text("muzzleSocket", "Muzzle Bone", muzzleSocket);
        fields.Float3("muzzleSocketOffset", "Muzzle Offset In Bone (m)", muzzleSocketOffset);
        fields.Float("muzzleForward", "Muzzle Forward Without Bone (m)", muzzleForward, 0.0f, 3.0f, 0.01f);
        fields.Float("muzzleHeight", "Muzzle Height Without Bone (m)", muzzleHeight, 0.0f, 3.0f, 0.01f);
        fields.Asset("muzzleVfx", "Muzzle Flash Effect", HIKARI::GameProjectAssetKind::VfxEffect, muzzleVfxGuid);
        fields.Asset("bulletVfx", "Bullet Tracer Effect", HIKARI::GameProjectAssetKind::VfxEffect, bulletVfxGuid);
        fields.Asset("smokeFieldVfx", "Smoke Field Effect", HIKARI::GameProjectAssetKind::VfxEffect, smokeFieldVfxGuid);
        fields.Float("smokeFieldRecenterDistance", "Smoke Field: Re-place When Idle Beyond (m)",
                     smokeFieldRecenterDistance, 0.5f, 50.0f, 0.1f);
        fields.Float("smokeFieldMaximumDistance", "Smoke Field: Always Re-place Beyond (m)", smokeFieldMaximumDistance,
                     0.5f, 50.0f, 0.1f);
        fields.Float("smokeFieldQuietSeconds", "Smoke Field: Idle Means No Shot For (s)", smokeFieldQuietSeconds, 0.0f,
                     10.0f, 0.1f);
    }
    if (fields.Section("Camera", true)) {
        fields.SceneObject("camera", "Camera", cameraObjectId);
        fields.Float("cameraPitch", "Pitch (deg)", cameraPitchDegrees, 10.0f, 89.0f, 0.5f);
        fields.Float("cameraYaw", "Yaw (deg)", cameraYawDegrees, -180.0f, 180.0f, 1.0f);
        fields.Float("cameraDistance", "Distance (m)", cameraDistance, 1.0f, 120.0f, 0.1f);
        fields.Float("cameraFov", "Field of View (deg)", cameraFovDegrees, 5.0f, 90.0f, 0.5f);
        fields.Float("cameraFocusHeight", "Focus Height (m)", cameraFocusHeight, 0.0f, 3.0f, 0.01f);
        fields.Float("cameraLookAhead", "Look Ahead (m)", cameraLookAhead, 0.0f, 6.0f, 0.05f);
        fields.Float("cameraFollowSeconds", "Follow Lag (s)", cameraFollowSeconds, 0.0f, 2.0f, 0.01f);
        fields.Float("cameraShakeAmplitude", "Shake Amplitude (m)", cameraShakeAmplitude, 0.0f, 3.0f, 0.01f);
        fields.Float("cameraShakeFrequency", "Shake Frequency (Hz)", cameraShakeFrequency, 1.0f, 60.0f, 0.5f);
        fields.Float("cameraShakeDecay", "Shake Decay (1/s)", cameraShakeDecay, 0.1f, 20.0f, 0.1f);
        fields.Float("fireShake", "Shake Per Shot", fireShake, 0.0f, 1.0f, 0.01f);
    }
    if (fields.Section("Input", false)) {
        fields.InputAction("moveAction", "Move", HIKARI::GameProjectInputValueType::Axis2D, moveAction);
        fields.InputAction("aimAction", "Aim (Gamepad)", HIKARI::GameProjectInputValueType::Axis2D, aimAction);
        fields.InputAction("fireAction", "Fire", HIKARI::GameProjectInputValueType::Button, fireAction);
        fields.InputAction("dodgeAction", "Dodge", HIKARI::GameProjectInputValueType::Button, dodgeAction);
        fields.InputAction("reloadAction", "Reload", HIKARI::GameProjectInputValueType::Button, reloadAction);
        fields.Bool("firesWithPointerButton", "Fire With Left Mouse Button", firesWithPointerButton);
    }
    if (fields.Section("Animation", false)) {
        fields.Text("idleClip", "Idle Clip", idleClip);
        fields.Text("walkClip", "Walk Clip", walkClip);
        fields.Text("runClip", "Run Clip", runClip);
        fields.Text("dodgeClip", "Dodge Clip", dodgeClip);
        fields.Text("fireClip", "Fire Clip (Upper Body)", fireClip);
        fields.Text("reloadClip", "Reload Clip (Upper Body)", reloadClip);
        fields.Text("upperBodyNode", "Upper Body Root Node", upperBodyNode);
        fields.Float("walkClipSpeed", "Walk Clip Ground Speed (m/s)", walkClipSpeed, 0.1f, 12.0f, 0.01f);
        fields.Float("runClipSpeed", "Run Clip Ground Speed (m/s)", runClipSpeed, 0.1f, 12.0f, 0.01f);
        fields.Float("runSpeedThreshold", "Run Above Speed (m/s)", runSpeedThreshold, 0.0f, 12.0f, 0.05f);
        fields.Float("dodgeClipSeconds", "Dodge Clip Length (s)", dodgeClipSeconds, 0.05f, 5.0f, 0.01f);
        fields.Float("fireClipSeconds", "Fire Clip Length (s)", fireClipSeconds, 0.02f, 2.0f, 0.005f);
        fields.Float("blendSeconds", "Blend Time (s)", blendSeconds, 0.0f, 1.0f, 0.01f);
        fields.Float("layerBlendSeconds", "Upper Body Blend Time (s)", layerBlendSeconds, 0.0f, 1.0f, 0.01f);
    }
}

} // namespace Project_AL4_PJ2
