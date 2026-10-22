#pragma once

#include <array>

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
    float playerHealth = 1.0f;
    // The full-window fade (S7): 0 draws nothing, 1 covers the window. Drawn
    // last, whether or not `visible`.
    float fade = 0.0f;
    std::array<float, 3> fadeColour{1.0f, 1.0f, 1.0f};
};

// Bottom-left bar: 240x12 at 1280x720, 24 px inset from the left and bottom
// edges. The width scales with the fraction, clamped to [0, 1]; the origin and
// height do not move.
HudRect hud_stamina_rect(float fraction);

// Player-health bar: 240x8 at 1280x720, at the stamina bar's 24 px left inset
// and one bar height plus a 24 px gap above its top edge — the STAMINA label
// occupies that gap (y = 652). The width scales with the fraction, clamped to
// [0, 1]; zero draws nothing.
HudRect hud_player_health_rect(float fraction);

// Top-edge bar: 600x8 at 1280x720, horizontally centred, 24 px inset from the
// top edge. The width scales with the fraction, clamped to [0, 1].
HudRect hud_health_rect(float fraction);

// Writes "STAMINA <rounded percent>" into `out` (e.g. 0.62 -> "STAMINA 62").
// The fraction is clamped to [0, 1] before rounding.
void hud_format_stamina(char (&out)[32], float fraction);

// The full-window fade quad: the whole 1280x720 layout rect with alpha =
// clamp(fade, 0, 1) (non-finite -> 0). Zero alpha means draw nothing.
struct HudFade {
    HudRect rect;
    float alpha;
};
HudFade hud_fade_rect(float fade);
