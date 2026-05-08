#include "ServerSession.h"

#include <iostream>
#include <thread>

#include "handlers/CommandDispatcher.h"
#include "net/SocketHelpers.h"

void handleClientSession(SOCKET clientSock)
{
    std::cout << "[Worker " << std::this_thread::get_id() << "] Client session started." << std::endl;

    while (true) {
        ParsedPacket request{};

        if (!recvPacket(clientSock, request)) {
            std::cout << "[Worker " << std::this_thread::get_id() << "] Client disconnected." << std::endl;
            break;
        }

        if (!dispatchCommand(clientSock, request)) {
            std::cerr << "[Worker " << std::this_thread::get_id() << "] Failed to handle command." << std::endl;
            break;
        }
    }

    closesocket(clientSock);
    std::cout << "[Worker " << std::this_thread::get_id() << "] Client session closed." << std::endl;
}
