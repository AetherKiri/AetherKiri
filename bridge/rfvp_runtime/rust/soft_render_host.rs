// SPDX-License-Identifier: MPL-2.0
// Included in the software renderer's module in the build-tree overlay.
impl SoftRenderer {
    fn try_host_axis_quad(
        &mut self,
        top_left: Vec2,
        top_right: Vec2,
        bottom_left: Vec2,
        uv0: Vec2,
        uv1: Vec2,
        color: Vec4,
        texture: TextureRef<'_>,
    ) -> bool {
        if top_left.x != bottom_left.x || top_left.y != top_right.y {
            return false;
        }
        let dx = top_right.x - top_left.x;
        let dy = bottom_left.y - top_left.y;
        if !top_left.is_finite() || !dx.is_finite() || !dy.is_finite() {
            return false;
        }
        if dx.abs() <= f32::EPSILON || dy.abs() <= f32::EPSILON {
            return true;
        }
        // Match the triangle rasterizer's pixel-center coverage, including
        // clipping and negative scales. A shared diagonal must be blended once.
        let first_x = ((top_left.x.min(top_right.x) - 0.5).ceil().max(0.0)) as u32;
        let first_y = ((top_left.y.min(bottom_left.y) - 0.5).ceil().max(0.0)) as u32;
        let end_x = ((top_left.x.max(top_right.x) - 0.5).floor() + 1.0)
            .clamp(0.0, self.framebuffer.width() as f32) as u32;
        let end_y = ((top_left.y.max(bottom_left.y) - 0.5).floor() + 1.0)
            .clamp(0.0, self.framebuffer.height() as f32) as u32;
        let du = (uv1.x - uv0.x) / dx;
        let dv = (uv1.y - uv0.y) / dy;
        for y in first_y..end_y {
            let v = uv0.y + (y as f32 + 0.5 - top_left.y) * dv;
            for x in first_x..end_x {
                let u = uv0.x + (x as f32 + 0.5 - top_left.x) * du;
                let src = self.sample_texture(texture, vec2(u, v)) * color;
                self.blend_pixel(x, y, src);
            }
        }
        true
    }
}

#[cfg(test)]
mod host_renderer_tests {
    use super::*;

    #[test]
    fn invisible_quads_skip_rasterization() {
        let mut renderer = SoftRenderer::new(16, 16, PixelFormat::Rgba8).unwrap();
        renderer.framebuffer.clear_rgba(9, 19, 29, 255);
        renderer.fill_rect(0.0, 0.0, 16.0, 16.0, vec4(1.0, 1.0, 1.0, 0.0));
        assert_eq!(renderer.stats.draw_calls, 0);
        for pixel in renderer.framebuffer.pixels().chunks_exact(4) {
            assert_eq!(pixel, &[9, 19, 29, 255]);
        }
    }

    #[test]
    fn translucent_quad_has_no_double_blended_diagonal() {
        let mut renderer = SoftRenderer::new(16, 16, PixelFormat::Rgba8).unwrap();
        renderer.framebuffer.clear_rgba(0, 0, 0, 255);
        renderer.fill_rect(0.0, 0.0, 16.0, 16.0, vec4(1.0, 1.0, 1.0, 0.5));
        for pixel in renderer.framebuffer.pixels().chunks_exact(4) {
            assert_eq!(pixel, &[128, 128, 128, 255]);
        }
    }

    #[test]
    fn axis_quad_matches_triangle_sampling_clipping_and_flips() {
        let image = DynamicImage::ImageRgba8(image::RgbaImage::from_fn(13, 11, |x, y| {
            image::Rgba([(x * 19) as u8, (y * 23) as u8, ((x + y) * 9) as u8, 255])
        }));
        let graph = GraphBuff::new();
        for format in [PixelFormat::Rgba8, PixelFormat::Bgra8] {
            for graph_id in [0, 4064] {
                for (x, y, w, h) in [
                    (0.0, 0.0, 16.0, 16.0),
                    (-3.3, 2.2, 20.5, 11.7),
                    (18.0, 17.0, -20.0, -19.0),
                    (40.0, 0.0, 4.0, 4.0),
                ] {
                    let mut actual = SoftRenderer::new(16, 16, format).unwrap();
                    let mut reference = SoftRenderer::new(16, 16, format).unwrap();
                    let texture = TextureRef::Graph {
                        graph_id,
                        graph: &graph,
                        image: &image,
                    };
                    let uv0 = vec2(0.13, 0.21);
                    let uv1 = vec2(0.87, 0.93);
                    let color = vec4(0.8, 0.9, 0.7, 1.0);
                    actual
                        .draw_textured_quad(
                            Mat4::from_translation(vec3(x, y, 0.0)),
                            w,
                            h,
                            uv0,
                            uv1,
                            color,
                            texture,
                        )
                        .unwrap();
                    let a = Vertex {
                        pos: vec2(x, y + h),
                        uv: vec2(uv0.x, uv1.y),
                        color,
                    };
                    let b = Vertex {
                        pos: vec2(x, y),
                        uv: uv0,
                        color,
                    };
                    let c = Vertex {
                        pos: vec2(x + w, y + h),
                        uv: uv1,
                        color,
                    };
                    let d = Vertex {
                        pos: vec2(x + w, y),
                        uv: vec2(uv1.x, uv0.y),
                        color,
                    };
                    reference.raster_triangle(a, b, c, texture);
                    reference.raster_triangle(c, b, d, texture);
                    for (a, b) in actual
                        .framebuffer
                        .pixels()
                        .iter()
                        .zip(reference.framebuffer.pixels())
                    {
                        assert!(a.abs_diff(*b) <= 1, "sampling mismatch {a} vs {b}");
                    }
                }
            }
        }
    }

    #[test]
    fn rotated_quad_uses_existing_triangle_path() {
        let mut renderer = SoftRenderer::new(16, 16, PixelFormat::Rgba8).unwrap();
        assert!(!renderer.try_host_axis_quad(
            vec2(1.0, 0.0),
            vec2(2.0, 1.0),
            vec2(0.0, 1.0),
            Vec2::ZERO,
            Vec2::ONE,
            Vec4::ONE,
            TextureRef::White
        ));
        assert!(renderer.framebuffer.pixels().iter().all(|v| *v == 0));
    }
}
