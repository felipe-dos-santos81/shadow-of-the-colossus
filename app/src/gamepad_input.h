#pragma once

// The gamepad's owned input (S10 Task 6a moved it out of app.cpp).

#include "gamepad_map.h"

#include <SDL3/SDL.h>

// The gamepad's owned input. Polled once per frame in the interactive loop;
// open/close track hot-plug. close() drops the handle and clears every input it
// owned, so an unplug mid-grip cannot latch the grip or a pause press on.
struct GamepadInput {
    SDL_Gamepad* handle = nullptr;
    SDL_JoystickID id = 0;
    float leftX = 0.0f;
    float leftY = 0.0f;
    float rightX = 0.0f;
    float rightY = 0.0f;
    bool grab = false;
    bool strike = false;
    bool pausePressed = false;
    bool pauseWasDown = false;
    // S8 (G4): south mounts/dismounts, north whistles (held state; the plain
    // takes the edges).
    bool south = false;
    bool north = false;
    bool raise = false;  // S9: the left shoulder raises the sword (held)

    ~GamepadInput();

    void open(SDL_JoystickID instance);

    void close();

    void poll();
};

// Opens the first already-connected gamepad (a later hot-plug arrives as an
// SDL_EVENT_GAMEPAD_ADDED in the loop).
void open_first_gamepad(GamepadInput& gamepad);
