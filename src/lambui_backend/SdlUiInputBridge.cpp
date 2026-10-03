#include "lambui_backend/SdlUiInputBridge.hpp"

#include <LambUI/UIManager.h>
#include <LambUI/UITypes.h>

namespace lambui_backend {

SdlUiInputBridge::PumpResult SdlUiInputBridge::Pump(LambUI::UIManager& uiManager,
                                                    const std::function<void(SDL_Keycode, uint32_t)>& onKeyDown) {
    using LambUI::MouseButton;
    namespace ScanCode = LambUI::ScanCode;

    PumpResult result;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            result.quitRequested = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            result.windowResized = true;
            result.windowWidth = event.window.data1;
            result.windowHeight = event.window.data2;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            uiManager.InjectMouseMove(event.motion.x, event.motion.y);
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            uiManager.InjectMouseLeave();
            break;
        case SDL_EVENT_WINDOW_MOUSE_ENTER: {
            float mouseX = 0.0f;
            float mouseY = 0.0f;
            SDL_GetMouseState(&mouseX, &mouseY);
            uiManager.InjectMouseMove(mouseX, mouseY);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
            uiManager.InjectMouseMove(event.wheel.mouse_x, event.wheel.mouse_y);
            uiManager.InjectMouseWheel(event.wheel.x * direction * 20.0f, event.wheel.y * direction * 20.0f);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            MouseButton mapped = MouseButton::Left;
            if (event.button.button == SDL_BUTTON_RIGHT) {
                mapped = MouseButton::Right;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                mapped = MouseButton::Middle;
            } else if (event.button.button != SDL_BUTTON_LEFT) {
                break;
            }
            uiManager.InjectMouseMove(event.button.x, event.button.y);
            uiManager.InjectMouseButton(mapped, event.button.down);
            break;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            uint32_t scanCode = 0;
            switch (event.key.key) {
            case SDLK_BACKSPACE:
                scanCode = ScanCode::Backspace;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                scanCode = ScanCode::Enter;
                break;
            case SDLK_ESCAPE:
                scanCode = ScanCode::Escape;
                break;
            case SDLK_SPACE:
                scanCode = ScanCode::Space;
                break;
            case SDLK_0:
                scanCode = '0';
                break;
            case SDLK_1:
                scanCode = '1';
                break;
            case SDLK_2:
                scanCode = '2';
                break;
            case SDLK_3:
                scanCode = '3';
                break;
            case SDLK_4:
                scanCode = '4';
                break;
            case SDLK_5:
                scanCode = '5';
                break;
            case SDLK_6:
                scanCode = '6';
                break;
            case SDLK_7:
                scanCode = '7';
                break;
            case SDLK_8:
                scanCode = '8';
                break;
            case SDLK_9:
                scanCode = '9';
                break;
            case SDLK_TAB:
                scanCode = ScanCode::Tab;
                break;
            case SDLK_LSHIFT:
                scanCode = ScanCode::LeftShift;
                break;
            case SDLK_RSHIFT:
                scanCode = ScanCode::RightShift;
                break;
            case SDLK_LCTRL:
                scanCode = ScanCode::LeftControl;
                break;
            case SDLK_RCTRL:
                scanCode = ScanCode::RightControl;
                break;
            case SDLK_A:
                scanCode = ScanCode::A;
                break;
            case SDLK_C:
                scanCode = ScanCode::C;
                break;
            case SDLK_X:
                scanCode = ScanCode::X;
                break;
            case SDLK_V:
                scanCode = ScanCode::V;
                break;
            case SDLK_LEFT:
                scanCode = ScanCode::Left;
                break;
            case SDLK_RIGHT:
                scanCode = ScanCode::Right;
                break;
            case SDLK_UP:
                scanCode = ScanCode::Up;
                break;
            case SDLK_DOWN:
                scanCode = ScanCode::Down;
                break;
            case SDLK_HOME:
                scanCode = ScanCode::Home;
                break;
            case SDLK_END:
                scanCode = ScanCode::End;
                break;
            case SDLK_DELETE:
                scanCode = ScanCode::Delete;
                break;
            default:
                break;
            }
            if (scanCode) {
                uiManager.InjectKeyEvent(scanCode, event.key.down);
            }
            if (event.type == SDL_EVENT_KEY_DOWN && onKeyDown) {
                onKeyDown(event.key.key, scanCode);
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            for (const char* character = event.text.text; *character != '\0'; ++character) {
                if (static_cast<unsigned char>(*character) < 0x80) {
                    uiManager.InjectCharacter(static_cast<char32_t>(*character));
                }
            }
            break;
        default:
            break;
        }
    }

    return result;
}

} // namespace lambui_backend
