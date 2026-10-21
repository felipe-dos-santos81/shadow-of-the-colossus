// HUD layout tests: the S4.1 stamina/health contract. Coordinates are
// design-space pixels on the 1280x720 reference frame, y down from the top;
// the renderer maps them to NDC, so the test pins the reference values the
// acceptance screenshot is measured against.
#include <doctest.h>

#include <cmath>
#include <string>

#include "hud.h"

TEST_CASE("hud bars clamp and scale") {
    HudRect s = hud_stamina_rect(0.5f);
    REQUIRE(s.w == doctest::Approx(120.0f));
    REQUIRE(hud_stamina_rect(-1.0f).w == 0.0f);
    REQUIRE(hud_stamina_rect(2.0f).w == doctest::Approx(240.0f));
    HudRect h = hud_health_rect(0.0f);
    REQUIRE(h.w == 0.0f);
    char label[32];
    hud_format_stamina(label, 0.62f);
    REQUIRE(std::string(label) == "STAMINA 62");
}

TEST_CASE("hud bars pin their reference corners") {
    // Stamina: bottom-left, 240x12 at 1280x720, 24 px inset.
    const HudRect stamina = hud_stamina_rect(1.0f);
    REQUIRE(stamina.x == doctest::Approx(24.0f));
    REQUIRE(stamina.y == doctest::Approx(684.0f));
    REQUIRE(stamina.w == doctest::Approx(240.0f));
    REQUIRE(stamina.h == doctest::Approx(12.0f));

    // Health: top edge, 600x8, centred horizontally, 24 px inset.
    const HudRect health = hud_health_rect(1.0f);
    REQUIRE(health.x == doctest::Approx(340.0f));
    REQUIRE(health.y == doctest::Approx(24.0f));
    REQUIRE(health.w == doctest::Approx(600.0f));
    REQUIRE(health.h == doctest::Approx(8.0f));

    // A half bar keeps the pinned origin and halves only the width.
    const HudRect half = hud_health_rect(0.5f);
    REQUIRE(half.x == doctest::Approx(340.0f));
    REQUIRE(half.w == doctest::Approx(300.0f));
}

TEST_CASE("hud player health bar clamps and scales above stamina") {
    // Player health: bottom-left as well, 240x8, one bar height plus the 24 px
    // gap above the stamina bar's top edge (684 - 8 - 24 = 652 at 1280x720).
    const HudRect full = hud_player_health_rect(1.0f);
    REQUIRE(full.x == doctest::Approx(24.0f));
    REQUIRE(full.y == doctest::Approx(652.0f));
    REQUIRE(full.w == doctest::Approx(240.0f));
    REQUIRE(full.h == doctest::Approx(8.0f));
    REQUIRE(full.y + full.h <= hud_stamina_rect(1.0f).y);

    // Only the width scales; the origin and height stay pinned.
    const HudRect half = hud_player_health_rect(0.5f);
    REQUIRE(half.x == doctest::Approx(24.0f));
    REQUIRE(half.y == doctest::Approx(652.0f));
    REQUIRE(half.w == doctest::Approx(120.0f));
    REQUIRE(half.h == doctest::Approx(8.0f));

    REQUIRE(hud_player_health_rect(0.0f).w == 0.0f);
    REQUIRE(hud_player_health_rect(-1.0f).w == 0.0f);
    REQUIRE(hud_player_health_rect(2.0f).w == doctest::Approx(240.0f));
    // A non-finite fraction never becomes a NaN bar.
    REQUIRE(hud_player_health_rect(std::nanf("")).w == 0.0f);
}

TEST_CASE("hud stamina label rounds and clamps") {
    char label[32];
    hud_format_stamina(label, 0.0f);
    REQUIRE(std::string(label) == "STAMINA 0");
    hud_format_stamina(label, 0.999f);
    REQUIRE(std::string(label) == "STAMINA 100");
    hud_format_stamina(label, 0.625f);
    REQUIRE(std::string(label) == "STAMINA 63");
    hud_format_stamina(label, -3.0f);
    REQUIRE(std::string(label) == "STAMINA 0");
    hud_format_stamina(label, 7.0f);
    REQUIRE(std::string(label) == "STAMINA 100");
}
