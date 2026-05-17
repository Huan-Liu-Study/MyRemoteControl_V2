#pragma once

#include <chrono>
#include <cstdint>
#include <winsock2.h>

enum class SessionChannel {
    Unknown,
    Control,
    Screen
};

enum class SessionState {
    Connected,
    ControlReady,
    ScreenReady,
    HandlingCommand,
    Streaming,
    Closing,
    Closed
};

struct ServerSessionContext {
    SOCKET clientSock = INVALID_SOCKET;
    SessionChannel channel = SessionChannel::Unknown;
    SessionState state = SessionState::Connected;
    std::chrono::steady_clock::time_point connectedAt = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastPacketAt = connectedAt;
    uint64_t packetsHandled = 0;
};
