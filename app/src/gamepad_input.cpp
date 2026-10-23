#include "gamepad_input.h"



void open_first_gamepad(GamepadInput& gamepad) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids == nullptr) {
        return;
    }
    for (int i = 0; i < count && gamepad.handle == nullptr; ++i) {
        gamepad.open(ids[i]);
    }
    SDL_free(ids);
}

GamepadInput::~GamepadInput() { close(); }

void GamepadInput::open(SDL_JoystickID instance) {
    close();
    handle = SDL_OpenGamepad(instance);
    id = handle != nullptr ? SDL_GetGamepadID(handle) : 0;
}

void GamepadInput::close() {
    if (handle != nullptr) {
        SDL_CloseGamepad(handle);
    }
    handle = nullptr;
    id = 0;
    leftX = leftY = rightX = rightY = 0.0f;
    grab = false;
    strike = false;
    pausePressed = false;
    pauseWasDown = false;
    south = false;
    north = false;
    raise = false;
}

void GamepadInput::poll() {
    leftX = leftY = rightX = rightY = 0.0f;
    grab = false;
    strike = false;
    pausePressed = false;
    south = false;
    north = false;
    raise = false;
    if (handle == nullptr) {
        return;
    }
    const auto axis = [this](SDL_GamepadAxis which) {
        return static_cast<float>(SDL_GetGamepadAxis(handle, which)) / 32767.0f;
    };
    leftX = axis(SDL_GAMEPAD_AXIS_LEFTX);
    leftY = axis(SDL_GAMEPAD_AXIS_LEFTY);
    rightX = axis(SDL_GAMEPAD_AXIS_RIGHTX);
    rightY = axis(SDL_GAMEPAD_AXIS_RIGHTY);
    grab = SDL_GetGamepadAxis(handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384;
    strike = SDL_GetGamepadButton(handle, kStrikeGamepadButton);
    south = SDL_GetGamepadButton(handle, SDL_GAMEPAD_BUTTON_SOUTH);
    north = SDL_GetGamepadButton(handle, SDL_GAMEPAD_BUTTON_NORTH);
    raise = SDL_GetGamepadButton(handle, kRaiseGamepadButton);
    const bool pauseNow = SDL_GetGamepadButton(handle, SDL_GAMEPAD_BUTTON_START);
    pausePressed = pauseNow && !pauseWasDown;
    pauseWasDown = pauseNow;
}
