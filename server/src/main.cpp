#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "ServerSession.h"
#include "concurrency/ThreadPool.h"

namespace {

void enableDpiAwareCoordinateSystem()
{
    SetProcessDPIAware();
}

} // namespace

int main() {
    enableDpiAwareCoordinateSystem();

    std::cout << "RemoteServer protocol build: packet-v2-threadpool" << std::endl;

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return -1;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        std::cerr << "socket() failed!" << std::endl;
        WSACleanup();
        return -1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(12345);

    if (bind(listenSock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed!" << std::endl;
        closesocket(listenSock);
        WSACleanup();
        return -1;
    }

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen() failed!" << std::endl;
        closesocket(listenSock);
        WSACleanup();
        return -1;
    }

    std::cout << "Server is listening on port 12345..." << std::endl;

    unsigned int workerCount = std::thread::hardware_concurrency();
    if (workerCount == 0) {
        workerCount = 4;
    }

    ThreadPool threadPool(workerCount);
    std::cout << "ThreadPool started with " << workerCount << " workers." << std::endl;

    while (true) {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);

        SOCKET clientSock = accept(listenSock, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);
        if (clientSock == INVALID_SOCKET) {
            continue;
        }

        std::cout << "Client connected. Dispatching session to thread pool." << std::endl;

        try {
            threadPool.enqueue(handleClientSession, clientSock);
        } catch (const std::exception& ex) {
            std::cerr << "Failed to enqueue client session: " << ex.what() << std::endl;
            closesocket(clientSock);
        }
    }
}
