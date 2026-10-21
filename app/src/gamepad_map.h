#pragma once

#include <SDL3/SDL_gamepad.h>

// The gamepad strike button (design §7.7): the *west* face button — X on an
// Xbox pad, square on a DualShock/DualSense — matching the keyboard J's
// charge-and-release strike. Pinned here so the mapping has one home and the
// unit test can assert west-not-south.
inline constexpr SDL_GamepadButton kStrikeGamepadButton = SDL_GAMEPAD_BUTTON_WEST;
