#include "net/IocpServer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>

#include "ServerSessionContext.h"
#include "handlers/CommandDispatcher.h"
#include "protocol/PacketCodec.h"

namespace {

constexpr DWORD ACCEPT_ADDRESS_BYTES = sizeof(sockaddr_in) + 16;
constexpr size_t MIN_ACCEPT_POSTS = 8;

enum class OperationType {
    Accept,
    Recv
};

enum class RecvStage {
    Header,
    Body
};

struct IoOperation {
    OVERLAPPED overlapped{};
    OperationType type;

    explicit IoOperation(OperationType operationType)
        : type(operationType)
    {
    }
};

struct AcceptOperation : IoOperation {
    SOCKET socket = INVALID_SOCKET;
    char addressBuffer[ACCEPT_ADDRESS_BYTES * 2]{};

    AcceptOperation()
        : IoOperation(OperationType::Accept)
    {
    }
};

struct ConnectionContext {
    SOCKET socket = INVALID_SOCKET;
    ServerSessionContext session{};
    IoOperation recvOperation{OperationType::Recv};
    RecvStage recvStage = RecvStage::Header;
    ByteBuffer headerBytes;
    ByteBuffer bodyBytes;
    size_t headerReceived = 0;
    size_t bodyReceived = 0;
    std::atomic_bool closing{false};
};

void resetOverlapped(OVERLAPPED& overlapped)
{
    std::memset(&overlapped, 0, sizeof(overlapped));
}

size_t workerCount()
{
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads == 0) {
        return 4;
    }

    return (std::max)(2u, hardwareThreads);
}

void closeConnection(ConnectionContext* connection)
{
    if (!connection) {
        return;
    }

    bool expected = false;
    if (!connection->closing.compare_exchange_strong(expected, true)) {
        return;
    }

    connection->session.state = SessionState::Closed;
    if (connection->socket != INVALID_SOCKET) {
        closesocket(connection->socket);
        connection->socket = INVALID_SOCKET;
    }

    delete connection;
}

bool parseCompletedPacket(ConnectionContext& connection, ParsedPacket& outPacket)
{
    PacketHeader header{};
    if (!PacketCodec::decodeHeader(connection.headerBytes, header)) {
        return false;
    }

    if (header.length < PACKET_HEADER_SIZE) {
        return false;
    }

    const uint32_t bodyLen = header.length - static_cast<uint32_t>(PACKET_HEADER_SIZE);
    if (bodyLen > MAX_BODY_SIZE) {
        return false;
    }

    return PacketCodec::unpack(header, connection.bodyBytes, outPacket);
}

} // namespace

struct IocpServer::Impl {
    explicit Impl(uint16_t serverPort)
        : port(serverPort)
    {
    }

    uint16_t port = 0;
    SOCKET listenSocket = INVALID_SOCKET;
    HANDLE completionPort = nullptr;
    LPFN_ACCEPTEX acceptEx = nullptr;
    std::atomic_bool running{false};
    std::vector<std::thread> workers;

    bool start()
    {
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed!" << std::endl;
            return false;
        }

        completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!completionPort) {
            std::cerr << "CreateIoCompletionPort failed!" << std::endl;
            WSACleanup();
            return false;
        }

        listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (listenSocket == INVALID_SOCKET) {
            std::cerr << "WSASocket listen failed!" << std::endl;
            stop();
            return false;
        }

        BOOL reuseAddress = TRUE;
        setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress));

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        serverAddr.sin_port = htons(port);

        if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "bind() failed!" << std::endl;
            stop();
            return false;
        }

        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "listen() failed!" << std::endl;
            stop();
            return false;
        }

        if (!loadAcceptEx()) {
            stop();
            return false;
        }

        if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(listenSocket), completionPort, 0, 0)) {
            std::cerr << "Bind listen socket to IOCP failed!" << std::endl;
            stop();
            return false;
        }

        running = true;
        const size_t count = workerCount();
        workers.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            workers.emplace_back([this]() {
                workerLoop();
            });
        }

        const size_t acceptPosts = (std::max)(MIN_ACCEPT_POSTS, count * 2);
        for (size_t i = 0; i < acceptPosts; ++i) {
            if (!postAccept()) {
                std::cerr << "Post AcceptEx failed during startup." << std::endl;
            }
        }

        std::cout << "IOCP server is listening on port " << port
                  << " with " << count << " workers." << std::endl;
        return true;
    }

    void run()
    {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    void stop()
    {
        running = false;

        if (listenSocket != INVALID_SOCKET) {
            closesocket(listenSocket);
            listenSocket = INVALID_SOCKET;
        }

        if (completionPort) {
            for (size_t i = 0; i < workers.size(); ++i) {
                PostQueuedCompletionStatus(completionPort, 0, 0, nullptr);
            }
        }

        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();

        if (completionPort) {
            CloseHandle(completionPort);
            completionPort = nullptr;
        }

        WSACleanup();
    }

    bool loadAcceptEx()
    {
        GUID guidAcceptEx = WSAID_ACCEPTEX;
        DWORD bytesReturned = 0;
        const int result = WSAIoctl(
            listenSocket,
            SIO_GET_EXTENSION_FUNCTION_POINTER,
            &guidAcceptEx,
            sizeof(guidAcceptEx),
            &acceptEx,
            sizeof(acceptEx),
            &bytesReturned,
            nullptr,
            nullptr
        );

        if (result == SOCKET_ERROR || !acceptEx) {
            std::cerr << "Load AcceptEx failed!" << std::endl;
            return false;
        }

        return true;
    }

    SOCKET createClientSocket()
    {
        return WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    }

    bool postAccept()
    {
        if (!running || listenSocket == INVALID_SOCKET || !acceptEx) {
            return false;
        }

        AcceptOperation* operation = new AcceptOperation;
        operation->socket = createClientSocket();
        if (operation->socket == INVALID_SOCKET) {
            delete operation;
            return false;
        }

        resetOverlapped(operation->overlapped);
        DWORD bytesReceived = 0;
        const BOOL ok = acceptEx(
            listenSocket,
            operation->socket,
            operation->addressBuffer,
            0,
            ACCEPT_ADDRESS_BYTES,
            ACCEPT_ADDRESS_BYTES,
            &bytesReceived,
            &operation->overlapped
        );

        if (!ok) {
            const int error = WSAGetLastError();
            if (error != ERROR_IO_PENDING) {
                closesocket(operation->socket);
                delete operation;
                return false;
            }
        }

        return true;
    }

    bool postRecv(ConnectionContext* connection)
    {
        if (!running || !connection || connection->closing || connection->socket == INVALID_SOCKET) {
            return false;
        }

        char* target = nullptr;
        DWORD remaining = 0;
        if (connection->recvStage == RecvStage::Header) {
            if (connection->headerBytes.size() != PACKET_HEADER_SIZE) {
                connection->headerBytes.assign(PACKET_HEADER_SIZE, 0);
            }
            target = reinterpret_cast<char*>(connection->headerBytes.data() + connection->headerReceived);
            remaining = static_cast<DWORD>(PACKET_HEADER_SIZE - connection->headerReceived);
        } else {
            target = reinterpret_cast<char*>(connection->bodyBytes.data() + connection->bodyReceived);
            remaining = static_cast<DWORD>(connection->bodyBytes.size() - connection->bodyReceived);
        }

        resetOverlapped(connection->recvOperation.overlapped);
        WSABUF buffer{};
        buffer.buf = target;
        buffer.len = remaining;
        DWORD flags = 0;
        DWORD bytesReceived = 0;
        const int result = WSARecv(
            connection->socket,
            &buffer,
            1,
            &bytesReceived,
            &flags,
            &connection->recvOperation.overlapped,
            nullptr
        );

        if (result == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                return false;
            }
        }

        return true;
    }

    void workerLoop()
    {
        while (running) {
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0;
            OVERLAPPED* overlapped = nullptr;
            const BOOL ok = GetQueuedCompletionStatus(
                completionPort,
                &bytesTransferred,
                &completionKey,
                &overlapped,
                INFINITE
            );

            if (!running && !overlapped) {
                return;
            }

            if (!overlapped) {
                continue;
            }

            IoOperation* operation = reinterpret_cast<IoOperation*>(overlapped);
            if (operation->type == OperationType::Accept) {
                handleAccept(static_cast<AcceptOperation*>(operation), ok);
                continue;
            }

            ConnectionContext* connection = reinterpret_cast<ConnectionContext*>(completionKey);
            handleRecv(connection, ok, bytesTransferred);
        }
    }

    void handleAccept(AcceptOperation* operation, BOOL ok)
    {
        if (!operation) {
            return;
        }

        SOCKET acceptedSocket = operation->socket;
        delete operation;

        if (!running) {
            if (acceptedSocket != INVALID_SOCKET) {
                closesocket(acceptedSocket);
            }
            return;
        }

        postAccept();

        if (!ok) {
            if (acceptedSocket != INVALID_SOCKET) {
                closesocket(acceptedSocket);
            }
            return;
        }

        setsockopt(
            acceptedSocket,
            SOL_SOCKET,
            SO_UPDATE_ACCEPT_CONTEXT,
            reinterpret_cast<const char*>(&listenSocket),
            sizeof(listenSocket)
        );

        ConnectionContext* connection = new ConnectionContext;
        connection->socket = acceptedSocket;
        connection->session.clientSock = acceptedSocket;
        connection->session.state = SessionState::Connected;
        connection->session.connectedAt = std::chrono::steady_clock::now();
        connection->session.lastPacketAt = connection->session.connectedAt;
        connection->recvStage = RecvStage::Header;
        connection->headerBytes.assign(PACKET_HEADER_SIZE, 0);

        if (!CreateIoCompletionPort(
                reinterpret_cast<HANDLE>(acceptedSocket),
                completionPort,
                reinterpret_cast<ULONG_PTR>(connection),
                0
            )) {
            closeConnection(connection);
            return;
        }

        std::cout << "Client connected by AcceptEx. Posting first WSARecv." << std::endl;
        if (!postRecv(connection)) {
            closeConnection(connection);
        }
    }

    void handleRecv(ConnectionContext* connection, BOOL ok, DWORD bytesTransferred)
    {
        if (!connection || !ok || bytesTransferred == 0) {
            closeConnection(connection);
            return;
        }

        if (connection->recvStage == RecvStage::Header) {
            connection->headerReceived += bytesTransferred;
            if (connection->headerReceived < PACKET_HEADER_SIZE) {
                if (!postRecv(connection)) {
                    closeConnection(connection);
                }
                return;
            }

            PacketHeader header{};
            if (!PacketCodec::decodeHeader(connection->headerBytes, header)
                || header.length < PACKET_HEADER_SIZE
                || header.length - PACKET_HEADER_SIZE > MAX_BODY_SIZE) {
                closeConnection(connection);
                return;
            }

            const size_t bodyLen = header.length - PACKET_HEADER_SIZE;
            connection->bodyBytes.assign(bodyLen, 0);
            connection->bodyReceived = 0;
            connection->recvStage = RecvStage::Body;

            if (bodyLen == 0) {
                handlePacketAndContinue(connection);
                return;
            }

            if (!postRecv(connection)) {
                closeConnection(connection);
            }
            return;
        }

        connection->bodyReceived += bytesTransferred;
        if (connection->bodyReceived < connection->bodyBytes.size()) {
            if (!postRecv(connection)) {
                closeConnection(connection);
            }
            return;
        }

        handlePacketAndContinue(connection);
    }

    void handlePacketAndContinue(ConnectionContext* connection)
    {
        ParsedPacket packet{};
        if (!parseCompletedPacket(*connection, packet)) {
            closeConnection(connection);
            return;
        }

        connection->session.lastPacketAt = std::chrono::steady_clock::now();
        if (!dispatchCommand(connection->session, packet)) {
            closeConnection(connection);
            return;
        }

        ++connection->session.packetsHandled;
        connection->headerReceived = 0;
        connection->bodyReceived = 0;
        connection->bodyBytes.clear();
        connection->recvStage = RecvStage::Header;
        connection->headerBytes.assign(PACKET_HEADER_SIZE, 0);

        if (!postRecv(connection)) {
            closeConnection(connection);
        }
    }
};

IocpServer::IocpServer(uint16_t port)
    : impl_(new Impl(port))
{
}

IocpServer::~IocpServer()
{
    stop();
    delete impl_;
}

bool IocpServer::start()
{
    return impl_->start();
}

void IocpServer::run()
{
    impl_->run();
}

void IocpServer::stop()
{
    if (impl_) {
        impl_->stop();
    }
}
