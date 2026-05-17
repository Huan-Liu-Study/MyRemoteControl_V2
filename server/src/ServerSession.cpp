#include "ServerSession.h"

#include <chrono>
#include <iostream>
#include <thread>

#include "handlers/CommandDispatcher.h"
#include "net/SocketHelpers.h"

void handleClientSession(SOCKET clientSock)
{
    std::cout << "[Worker " << std::this_thread::get_id() << "] Client session started." << std::endl;

    ServerSessionContext session{};
    session.clientSock = clientSock;
    session.state = SessionState::Connected;
    session.connectedAt = std::chrono::steady_clock::now();
    session.lastPacketAt = session.connectedAt;

    while (true) {
        ParsedPacket request{};

        if (!recvPacket(clientSock, request)) {
            std::cout << "[Worker " << std::this_thread::get_id() << "] Client disconnected." << std::endl;
            session.state = SessionState::Closing;
            break;
        }
        session.lastPacketAt = std::chrono::steady_clock::now();

        if (!dispatchCommand(session, request)) {
            std::cerr << "[Worker " << std::this_thread::get_id() << "] Failed to handle command." << std::endl;
            session.state = SessionState::Closing;
            break;
        }

        ++session.packetsHandled;
    }

    session.state = SessionState::Closed;
    closesocket(clientSock);
    std::cout << "[Worker " << std::this_thread::get_id() << "] Client session closed." << std::endl;
}
