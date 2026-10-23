#include "hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// The reference frame the layout is authored on: every rect below is a pixel
// value at 1280x720, and the renderer scales it to the drawable, so a HUD is
// always the same fraction of the screen.
constexpr float kDesignWidth = 1280.0f;
constexpr float kDesignHeight = 720.0f;

// Margin between the bars and the screen edges.
constexpr float kInset = 24.0f;

// Bar sizes at the reference resolution.
constexpr float kStaminaWidth = 240.0f;
constexpr float kStaminaHeight = 12.0f;
constexpr float kPlayerHealthWidth = 240.0f;
constexpr float kPlayerHealthHeight = 8.0f;
constexpr float kHealthWidth = 600.0f;
constexpr float kHealthHeight = 8.0f;

// Vertical gap between the player-health bar's bottom edge and the stamina
// bar's top edge (y = 684 - 24 - 8 = 652 at the reference resolution).
constexpr float kPlayerHealthGap = 24.0f;

float clamp_fraction(float fraction) {
    if (!std::isfinite(fraction)) {
        return 0.0f;
    }
    return std::clamp(fraction, 0.0f, 1.0f);
}

}  // namespace

HudRect hud_stamina_rect(float fraction) {
    const float clamped = clamp_fraction(fraction);
    return HudRect{kInset, kDesignHeight - kInset - kStaminaHeight, kStaminaWidth * clamped,
                   kStaminaHeight};
}

HudRect hud_health_rect(float fraction) {
    const float clamped = clamp_fraction(fraction);
    return HudRect{(kDesignWidth - kHealthWidth) * 0.5f, kInset, kHealthWidth * clamped,
                   kHealthHeight};
}

HudRect hud_player_health_rect(float fraction) {
    const float clamped = clamp_fraction(fraction);
    return HudRect{kInset,
                   kDesignHeight - kInset - kStaminaHeight - kPlayerHealthGap -
                       kPlayerHealthHeight,
                   kPlayerHealthWidth * clamped, kPlayerHealthHeight};
}

HudFade hud_fade_rect(float fade) {
    return HudFade{HudRect{0.0f, 0.0f, kDesignWidth, kDesignHeight}, clamp_fraction(fade)};
}

void hud_format_stamina(char (&out)[32], float fraction) {
    const long percent = std::lround(clamp_fraction(fraction) * 100.0f);
    std::snprintf(out, sizeof(out), "STAMINA %ld", percent);
}
