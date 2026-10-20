#pragma once

// HUD layout and label formatting for the S4.1 stamina/health overlay.
//
// Rectangles are in *design-space* pixels on a 1280x720 reference frame, with
// y growing downward from the top edge (the convention a 2D HUD is authored
// in). The renderer maps them to NDC, so the same layout scales with the
// window instead of pinning to one resolution.
struct HudRect {
    float x, y, w, h;
};

struct HudState {
    bool visible = false;
    float stamina = 1.0f;
    float health = 1.0f;
};

// Bottom-left bar: 240x12 at 1280x720, 24 px inset from the left and bottom
// edges. The width scales with the fraction, clamped to [0, 1]; the origin and
// height do not move.
HudRect hud_stamina_rect(float fraction);

// Top-edge bar: 600x8 at 1280x720, horizontally centred, 24 px inset from the
// top edge. The width scales with the fraction, clamped to [0, 1].
HudRect hud_health_rect(float fraction);

// Writes "STAMINA <rounded percent>" into `out` (e.g. 0.62 -> "STAMINA 62").
// The fraction is clamped to [0, 1] before rounding.
void hud_format_stamina(char (&out)[32], float fraction);
