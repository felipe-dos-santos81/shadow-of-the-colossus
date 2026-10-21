// The gamepad strike mapping (S4.2 design §7.7): the strike is the *west* face
// button — X on an Xbox pad, square on a DualShock/DualSense — matching the
// keyboard J's charge-and-release strike. This test pins west-not-south so the
// mapping cannot silently revert to the S4.1 A/cross binding.
#include <doctest.h>

#include <SDL3/SDL.h>

#include "gamepad_map.h"

TEST_CASE("gamepad strike is the west face button") {
    REQUIRE(kStrikeGamepadButton == SDL_GAMEPAD_BUTTON_WEST);
    REQUIRE(kStrikeGamepadButton != SDL_GAMEPAD_BUTTON_SOUTH);
}
