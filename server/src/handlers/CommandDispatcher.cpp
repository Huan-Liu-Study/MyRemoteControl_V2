#include "handlers/CommandDispatcher.h"

#include <chrono>
#include <functional>
#include <iostream>
#include <unordered_map>

#include "handlers/DriveHandler.h"
#include "handlers/FileHandler.h"
#include "handlers/InputHandler.h"
#include "handlers/ScreenHandler.h"
#include "net/SocketHelpers.h"
#include "protocol/Command.h"
#include "protocol/Messages.h"

namespace {

const char* channelName(SessionChannel channel)
{
    switch (channel) {
    case SessionChannel::Control:
        return "control";
    case SessionChannel::Screen:
        return "screen";
    default:
        return "unknown";
    }
}

const char* stateName(SessionState state)
{
    switch (state) {
    case SessionState::Connected:
        return "connected";
    case SessionState::ControlReady:
        return "control-ready";
    case SessionState::ScreenReady:
        return "screen-ready";
    case SessionState::HandlingCommand:
        return "handling-command";
    case SessionState::Streaming:
        return "streaming";
    case SessionState::Closing:
        return "closing";
    case SessionState::Closed:
        return "closed";
    default:
        return "unknown";
    }
}

SessionChannel commandChannel(CMD::Type command)
{
    switch (command) {
    case CMD::CMD_SESSION_HELLO:
    case CMD::CMD_SESSION_HEARTBEAT:
        return SessionChannel::Unknown;

    case CMD::CMD_LIST_DRIVES:
    case CMD::CMD_LIST_DIR:
    case CMD::CMD_DOWNLOAD_START:
    case CMD::CMD_MOUSE_MOVE:
    case CMD::CMD_MOUSE_CLICK:
    case CMD::CMD_MOUSE_POSITION:
    case CMD::CMD_KEYBOARD_EVENT:
    case CMD::CMD_MOUSE_WHEEL:
        return SessionChannel::Control;

    case CMD::CMD_SCREENSHOT_START:
    case CMD::CMD_SCREEN_STREAM_START:
    case CMD::CMD_SCREEN_STREAM_STOP:
    case CMD::CMD_SCREEN_STREAM_KEYFRAME_REQUEST:
    case CMD::CMD_SCREEN_STREAM_FRAME_ACK:
        return SessionChannel::Screen;

    default:
        return SessionChannel::Unknown;
    }
}

SessionChannel helloChannel(uint32_t channel)
{
    if (channel == SESSION_CHANNEL_CONTROL) {
        return SessionChannel::Control;
    }

    if (channel == SESSION_CHANNEL_SCREEN) {
        return SessionChannel::Screen;
    }

    return SessionChannel::Unknown;
}

SessionState readyStateFor(SessionChannel channel)
{
    return channel == SessionChannel::Screen
        ? SessionState::ScreenReady
        : SessionState::ControlReady;
}

bool ensureSessionChannel(ServerSessionContext& session, CMD::Type command)
{
    const SessionChannel requiredChannel = commandChannel(command);
    if (requiredChannel == SessionChannel::Unknown) {
        return true;
    }

    if (session.channel == SessionChannel::Unknown) {
        session.channel = requiredChannel;
        session.state = readyStateFor(requiredChannel);
        std::cout << "Session channel selected: " << channelName(session.channel) << std::endl;
        return true;
    }

    return session.channel == requiredChannel;
}

bool rejectCommand(ServerSessionContext& session, const std::string& message)
{
    std::cerr << "Reject command on channel=" << channelName(session.channel)
              << ", state=" << stateName(session.state)
              << ": " << message << std::endl;
    return sendPacket(session.clientSock, CMD::CMD_ERROR, message);
}

bool handleSessionHello(ServerSessionContext& session, const ByteBuffer& payload)
{
    SessionHelloRequest request{};
    SessionHelloResponse response{};
    response.protocolVersion = PROTOCOL_VERSION;

    if (!deserializeSessionHelloRequest(payload, request)) {
        response.ok = 0;
        response.errorMessage = "Invalid session hello request.";
        return sendPacket(session.clientSock, CMD::CMD_SESSION_HELLO, serializeSessionHelloResponse(response));
    }

    if (request.protocolVersion != PROTOCOL_VERSION) {
        response.ok = 0;
        response.errorMessage = "Unsupported protocol version.";
        return sendPacket(session.clientSock, CMD::CMD_SESSION_HELLO, serializeSessionHelloResponse(response));
    }

    const SessionChannel requestedChannel = helloChannel(request.channel);
    if (requestedChannel == SessionChannel::Unknown) {
        response.ok = 0;
        response.errorMessage = "Invalid session channel.";
        return sendPacket(session.clientSock, CMD::CMD_SESSION_HELLO, serializeSessionHelloResponse(response));
    }

    if (session.channel != SessionChannel::Unknown || session.state != SessionState::Connected) {
        response.ok = 0;
        response.errorMessage = "Session hello has already been completed.";
        return sendPacket(session.clientSock, CMD::CMD_SESSION_HELLO, serializeSessionHelloResponse(response));
    }

    session.channel = requestedChannel;
    session.state = readyStateFor(requestedChannel);
    response.ok = 1;
    response.errorMessage.clear();
    std::cout << "Session hello accepted: channel=" << channelName(session.channel) << std::endl;
    return sendPacket(session.clientSock, CMD::CMD_SESSION_HELLO, serializeSessionHelloResponse(response));
}

bool handleSessionHeartbeat(ServerSessionContext& session)
{
    if (session.channel == SessionChannel::Unknown) {
        return rejectCommand(session, "Session hello is required before heartbeat.");
    }

    if (session.state == SessionState::Streaming) {
        return rejectCommand(session, "Heartbeat is not accepted while streaming.");
    }

    session.lastPacketAt = std::chrono::steady_clock::now();
    return sendPacket(session.clientSock, CMD::CMD_SESSION_HEARTBEAT);
}

using CommandHandler = std::function<bool(ServerSessionContext&, const ParsedPacket&)>;

bool finishCommand(ServerSessionContext& session, SessionState readyState, bool ok)
{
    session.state = ok ? readyState : SessionState::Closing;
    return ok;
}

bool handleListDrivesCommand(ServerSessionContext& session, const ParsedPacket&)
{
    std::cout << "Received CMD_LIST_DRIVES command." << std::endl;
    return handleListDrives(session.clientSock);
}

bool handleListDirectoryCommand(ServerSessionContext& session, const ParsedPacket& request)
{
    std::cout << "Received CMD_LIST_DIR command." << std::endl;
    return handleListDirectory(session.clientSock, request.payload);
}

bool handleDownloadStartCommand(ServerSessionContext& session, const ParsedPacket& request)
{
    std::cout << "Received CMD_DOWNLOAD_START command." << std::endl;
    return handleDownloadStart(session.clientSock, request.payload);
}

bool handleMouseMoveCommand(ServerSessionContext& session, const ParsedPacket& request)
{
    std::cout << "Received CMD_MOUSE_MOVE command." << std::endl;
    return handleMouseMove(session.clientSock, request.payload);
}

bool handleMouseClickCommand(ServerSessionContext& session, const ParsedPacket& request)
{
    std::cout << "Received CMD_MOUSE_CLICK command." << std::endl;
    return handleMouseClick(session.clientSock, request.payload);
}

bool handleMousePositionCommand(ServerSessionContext& session, const ParsedPacket&)
{
    std::cout << "Received CMD_MOUSE_POSITION command." << std::endl;
    return handleMousePosition(session.clientSock);
}

bool handleKeyboardEventCommand(ServerSessionContext& session, const ParsedPacket& request)
{
    std::cout << "Received CMD_KEYBOARD_EVENT command." << std::endl;
    return handleKeyboardEvent(session.clientSock, request.payload);
}

bool handleMouseWheelCommand(ServerSessionContext& session, const ParsedPacket& request)
{
    std::cout << "Received CMD_MOUSE_WHEEL command." << std::endl;
    return handleMouseWheel(session.clientSock, request.payload);
}

bool handleScreenshotStartCommand(ServerSessionContext& session, const ParsedPacket& request)
{
    std::cout << "Received CMD_SCREENSHOT_START command." << std::endl;
    return handleScreenshotStart(session.clientSock, request.payload);
}

bool handleScreenStreamStartCommand(ServerSessionContext& session, const ParsedPacket& request)
{
    std::cout << "Received CMD_SCREEN_STREAM_START command." << std::endl;
    session.state = SessionState::Streaming;
    return handleScreenStreamStart(session.clientSock, request.payload);
}

bool rejectStreamControlOutsideStreaming(ServerSessionContext& session, const ParsedPacket&)
{
    session.state = readyStateFor(session.channel);
    return rejectCommand(session, "Stream control command is only valid while streaming.");
}

const std::unordered_map<CMD::Type, CommandHandler>& commandHandlers()
{
    static const std::unordered_map<CMD::Type, CommandHandler> handlers = {
        {CMD::CMD_LIST_DRIVES, handleListDrivesCommand},
        {CMD::CMD_LIST_DIR, handleListDirectoryCommand},
        {CMD::CMD_DOWNLOAD_START, handleDownloadStartCommand},
        {CMD::CMD_MOUSE_MOVE, handleMouseMoveCommand},
        {CMD::CMD_MOUSE_CLICK, handleMouseClickCommand},
        {CMD::CMD_MOUSE_POSITION, handleMousePositionCommand},
        {CMD::CMD_KEYBOARD_EVENT, handleKeyboardEventCommand},
        {CMD::CMD_MOUSE_WHEEL, handleMouseWheelCommand},
        {CMD::CMD_SCREENSHOT_START, handleScreenshotStartCommand},
        {CMD::CMD_SCREEN_STREAM_START, handleScreenStreamStartCommand},
        {CMD::CMD_SCREEN_STREAM_STOP, rejectStreamControlOutsideStreaming},
        {CMD::CMD_SCREEN_STREAM_KEYFRAME_REQUEST, rejectStreamControlOutsideStreaming},
        {CMD::CMD_SCREEN_STREAM_FRAME_ACK, rejectStreamControlOutsideStreaming}
    };

    return handlers;
}

} // namespace

bool dispatchCommand(ServerSessionContext& session, const ParsedPacket& request)
{
    const auto command = static_cast<CMD::Type>(request.header.command);

    if (command == CMD::CMD_SESSION_HELLO) {
        return handleSessionHello(session, request.payload);
    }

    if (command == CMD::CMD_SESSION_HEARTBEAT) {
        return handleSessionHeartbeat(session);
    }

    if (session.channel == SessionChannel::Unknown) {
        return rejectCommand(session, "Session hello is required before other commands.");
    }

    if (!ensureSessionChannel(session, command)) {
        return rejectCommand(session, "Command does not belong to this session channel.");
    }

    const SessionState readyState = readyStateFor(session.channel);
    if (session.state != SessionState::Connected && session.state != readyState) {
        return rejectCommand(session, "Session is not ready for a new command.");
    }

    session.state = SessionState::HandlingCommand;

    const auto& handlers = commandHandlers();
    const auto handler = handlers.find(command);
    if (handler != handlers.end()) {
        return finishCommand(session, readyState, handler->second(session, request));
    }

    std::cout << "Received unknown command: " << request.header.command << std::endl;
    session.state = readyState;
    return sendPacket(session.clientSock, CMD::CMD_ERROR, "Unknown command.");
}
