#include "handlers/InputHandler.h"

#include <string>
#include <windows.h>

#include "net/SocketHelpers.h"
#include "protocol/Command.h"
#include "protocol/Messages.h"

namespace {

constexpr uint32_t MOUSE_BUTTON_LEFT = 1;
constexpr uint32_t MOUSE_BUTTON_RIGHT = 2;
constexpr uint32_t MOUSE_BUTTON_MIDDLE = 3;

constexpr uint32_t MOUSE_ACTION_DOWN = 1;
constexpr uint32_t MOUSE_ACTION_UP = 2;
constexpr uint32_t MOUSE_ACTION_CLICK = 3;

constexpr uint32_t KEY_ACTION_DOWN = 1;
constexpr uint32_t KEY_ACTION_UP = 2;

DWORD mouseDownFlag(uint32_t button)
{
    if (button == MOUSE_BUTTON_LEFT) {
        return MOUSEEVENTF_LEFTDOWN;
    }

    if (button == MOUSE_BUTTON_RIGHT) {
        return MOUSEEVENTF_RIGHTDOWN;
    }

    if (button == MOUSE_BUTTON_MIDDLE) {
        return MOUSEEVENTF_MIDDLEDOWN;
    }

    return 0;
}

DWORD mouseUpFlag(uint32_t button)
{
    if (button == MOUSE_BUTTON_LEFT) {
        return MOUSEEVENTF_LEFTUP;
    }

    if (button == MOUSE_BUTTON_RIGHT) {
        return MOUSEEVENTF_RIGHTUP;
    }

    if (button == MOUSE_BUTTON_MIDDLE) {
        return MOUSEEVENTF_MIDDLEUP;
    }

    return 0;
}

bool sendOk(SOCKET clientSock, CMD::Type command)
{
    return sendPacket(clientSock, command, "OK");
}

std::string windowsErrorText(const std::string& action)
{
    return action + " failed, error: " + std::to_string(GetLastError());
}

} // namespace

bool handleMouseMove(SOCKET clientSock, const ByteBuffer& requestPayload)
{
    MouseMoveRequest request{};
    if (!deserializeMouseMoveRequest(requestPayload, request)) {
        return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid mouse move request.");
    }

    if (!SetCursorPos(request.x, request.y)) {
        return sendPacket(clientSock, CMD::CMD_ERROR, windowsErrorText("SetCursorPos"));
    }

    return sendOk(clientSock, CMD::CMD_MOUSE_MOVE);
}

bool handleMouseClick(SOCKET clientSock, const ByteBuffer& requestPayload)
{
    MouseClickRequest request{};
    if (!deserializeMouseClickRequest(requestPayload, request)) {
        return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid mouse click request.");
    }

    const DWORD downFlag = mouseDownFlag(request.button);
    const DWORD upFlag = mouseUpFlag(request.button);

    if (downFlag == 0 || upFlag == 0) {
        return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid mouse button.");
    }

    if (request.action == MOUSE_ACTION_DOWN) {
        mouse_event(downFlag, 0, 0, 0, 0);
        return sendOk(clientSock, CMD::CMD_MOUSE_CLICK);
    }

    if (request.action == MOUSE_ACTION_UP) {
        mouse_event(upFlag, 0, 0, 0, 0);
        return sendOk(clientSock, CMD::CMD_MOUSE_CLICK);
    }

    if (request.action == MOUSE_ACTION_CLICK) {
        mouse_event(downFlag, 0, 0, 0, 0);
        mouse_event(upFlag, 0, 0, 0, 0);
        return sendOk(clientSock, CMD::CMD_MOUSE_CLICK);
    }

    return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid mouse action.");
}

bool handleMousePosition(SOCKET clientSock)
{
    POINT point{};
    if (!GetCursorPos(&point)) {
        return sendPacket(clientSock, CMD::CMD_ERROR, windowsErrorText("GetCursorPos"));
    }

    MousePositionResponse response{};
    response.x = point.x;
    response.y = point.y;

    return sendPacket(clientSock, CMD::CMD_MOUSE_POSITION, serializeMousePositionResponse(response));
}

bool handleKeyboardEvent(SOCKET clientSock, const ByteBuffer& requestPayload)
{
    KeyboardEventRequest request{};
    if (!deserializeKeyboardEventRequest(requestPayload, request)) {
        return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid keyboard event request.");
    }

    if (request.virtualKey == 0 || request.virtualKey > 255) {
        return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid virtual key.");
    }

    const BYTE virtualKey = static_cast<BYTE>(request.virtualKey);
    const BYTE scanCode = static_cast<BYTE>(MapVirtualKeyA(virtualKey, MAPVK_VK_TO_VSC));

    if (request.action == KEY_ACTION_DOWN) {
        keybd_event(virtualKey, scanCode, 0, 0);
        return sendOk(clientSock, CMD::CMD_KEYBOARD_EVENT);
    }

    if (request.action == KEY_ACTION_UP) {
        keybd_event(virtualKey, scanCode, KEYEVENTF_KEYUP, 0);
        return sendOk(clientSock, CMD::CMD_KEYBOARD_EVENT);
    }

    return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid keyboard action.");
}

bool handleMouseWheel(SOCKET clientSock, const ByteBuffer& requestPayload)
{
    MouseWheelRequest request{};
    if (!deserializeMouseWheelRequest(requestPayload, request)) {
        return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid mouse wheel request.");
    }

    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(request.delta), 0);
    return sendOk(clientSock, CMD::CMD_MOUSE_WHEEL);
}
