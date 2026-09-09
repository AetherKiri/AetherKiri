// SPDX-License-Identifier: MPL-2.0
// Included in MotionManager's module in the build-tree overlay.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub(crate) struct HostMotionSnapshot {
    alpha: AlphaMotionContainer,
    position: MoveMotionContainer,
    rotation: RotationMotionContainer,
    scale: ScaleMotionContainer,
    z: ZMotionContainer,
    camera: V3dMotionContainer,
    sprite: SpriteAnimContainer,
    snow: SnowMotionContainer,
    lip: LipMotionContainer,
}

impl MotionManager {
    pub(crate) fn capture_host_motions(&self) -> HostMotionSnapshot {
        HostMotionSnapshot {
            alpha: self.alpha_motion_container.clone(),
            position: self.move_motion_container.clone(),
            rotation: self.rotation_motion_container.clone(),
            scale: self.scale_motion_container.clone(),
            z: self.z_motion_container.clone(),
            camera: self.v3d_motion_container.clone(),
            sprite: self.sprite_anim_container.clone(),
            snow: self.snow_motion_container.clone(),
            lip: self.lip_motion_container.clone(),
        }
    }

    pub(crate) fn apply_host_motions(&mut self, state: &HostMotionSnapshot) {
        // Timelines store relative elapsed milliseconds, not wall-clock instants.
        // Restore them after the base snapshot has reset the old session's motions.
        self.alpha_motion_container = state.alpha.clone();
        self.move_motion_container = state.position.clone();
        self.rotation_motion_container = state.rotation.clone();
        self.scale_motion_container = state.scale.clone();
        self.z_motion_container = state.z.clone();
        self.v3d_motion_container = state.camera.clone();
        self.sprite_anim_container = state.sprite.clone();
        self.snow_motion_container = state.snow.clone();
        self.lip_motion_container = state.lip.clone();
    }
}
