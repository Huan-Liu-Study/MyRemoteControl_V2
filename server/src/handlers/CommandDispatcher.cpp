#include "handlers/CommandDispatcher.h"

#include <iostream>

#include "handlers/DriveHandler.h"
#include "handlers/FileHandler.h"
#include "handlers/InputHandler.h"
#include "net/SocketHelpers.h"
#include "protocol/Command.h"

bool dispatchCommand(SOCKET clientSock, const ParsedPacket& request)
{
    if (request.header.command == CMD::CMD_LIST_DRIVES) {
        std::cout << "Received CMD_LIST_DRIVES command." << std::endl;
        return handleListDrives(clientSock);
    }

    if (request.header.command == CMD::CMD_LIST_DIR) {
        std::cout << "Received CMD_LIST_DIR command." << std::endl;
        return handleListDirectory(clientSock, request.payload);
    }

    if (request.header.command == CMD::CMD_DOWNLOAD_START) {
        std::cout << "Received CMD_DOWNLOAD_START command." << std::endl;
        return handleDownloadStart(clientSock, request.payload);
    }

    if (request.header.command == CMD::CMD_MOUSE_MOVE) {
        std::cout << "Received CMD_MOUSE_MOVE command." << std::endl;
        return handleMouseMove(clientSock, request.payload);
    }

    if (request.header.command == CMD::CMD_MOUSE_CLICK) {
        std::cout << "Received CMD_MOUSE_CLICK command." << std::endl;
        return handleMouseClick(clientSock, request.payload);
    }

    if (request.header.command == CMD::CMD_MOUSE_POSITION) {
        std::cout << "Received CMD_MOUSE_POSITION command." << std::endl;
        return handleMousePosition(clientSock);
    }

    std::cout << "Received unknown command: " << request.header.command << std::endl;
    return sendPacket(clientSock, CMD::CMD_ERROR, "Unknown command.");
}
