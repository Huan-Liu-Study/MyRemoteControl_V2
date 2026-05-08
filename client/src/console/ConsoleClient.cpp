#include "console/ConsoleClient.h"

#include <fstream>
#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "net/SocketHelpers.h"
#include "protocol/Command.h"
#include "protocol/Messages.h"
#include "protocol/PacketCodec.h"

namespace {

void printHelp()
{
    std::cout << "Commands:" << std::endl;
    std::cout << "  drives          list remote drives" << std::endl;
    std::cout << "  dir <path>      list remote directory, example: dir C:\\" << std::endl;
    std::cout << "  download <path> download remote small file" << std::endl;
    std::cout << "  quit            exit client" << std::endl;
}

bool sendListDrivesRequest(SOCKET clientSock)
{
    return sendPacket(clientSock, CMD::CMD_LIST_DRIVES);
}

bool sendListDirRequest(SOCKET clientSock, const std::string& path)
{
    ListDirRequest request{};
    request.path = path;

    return sendPacket(clientSock, CMD::CMD_LIST_DIR, serializeListDirRequest(request));
}

bool sendDownloadStartRequest(SOCKET clientSock, const std::string& path)
{
    DownloadStartRequest request{};
    request.path = path;

    return sendPacket(clientSock, CMD::CMD_DOWNLOAD_START, serializeDownloadStartRequest(request));
}

std::string trimQuotes(const std::string& text)
{
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return text.substr(1, text.size() - 2);
    }

    return text;
}

bool handleDriveListResponse(const ByteBuffer& payload)
{
    DriveListResponse decoded{};
    if (!deserializeDriveListResponse(payload, decoded)) {
        std::cerr << "Failed to parse drive list response." << std::endl;
        return false;
    }

    for (const std::string& drive : decoded.drives) {
        std::cout << "Drive: " << drive << std::endl;
    }

    return true;
}

bool handleListDirResponse(const ByteBuffer& payload)
{
    ListDirResponse decoded{};
    if (!deserializeListDirResponse(payload, decoded)) {
        std::cerr << "Failed to parse list dir response." << std::endl;
        return false;
    }

    for (const FileEntry& entry : decoded.entries) {
        std::cout << (entry.isDirectory ? "[DIR]  " : "[FILE] ");
        std::cout << entry.name << std::endl;
    }

    return true;
}

bool handleDownloadStartResponse(SOCKET clientSock, const ByteBuffer& payload)
{
    DownloadStartResponse decoded{};
    if (!deserializeDownloadStartResponse(payload, decoded)) {
        std::cerr << "Failed to parse download start response." << std::endl;
        return false;
    }

    if (!decoded.ok) {
        std::cerr << "Server denied download: " << decoded.errorMessage << std::endl;
        return true;
    }

    std::string localFileName = decoded.fileName.empty() ? "download.bin" : decoded.fileName;
    std::ofstream out(localFileName, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to create local file: " << localFileName << std::endl;
        return true;
    }

    std::cout << "\n[Downloading] " << localFileName
              << " (Size: " << decoded.fileSize << " bytes)\n" << std::endl;

    uint64_t totalReceived = 0;

    while (true) {
        ParsedPacket chunkPacket{};

        if (!recvPacket(clientSock, chunkPacket)) {
            std::cerr << "\n[!] Network error during file download." << std::endl;
            return false;
        }

        if (chunkPacket.header.command == CMD::CMD_DOWNLOAD_CHUNK) {
            out.write(reinterpret_cast<const char*>(chunkPacket.payload.data()), chunkPacket.payload.size());
            totalReceived += chunkPacket.payload.size();
            std::cout << "\rProgress: " << totalReceived << " / "
                      << decoded.fileSize << " bytes" << std::flush;
        } else if (chunkPacket.header.command == CMD::CMD_DOWNLOAD_END) {
            std::cout << "\n\nDownload complete." << std::endl;
            out.close();
            break;
        } else if (chunkPacket.header.command == CMD::CMD_ERROR) {
            std::cerr << "\n[!] Server reported error during transfer." << std::endl;
            break;
        } else {
            std::cerr << "\n[!] Unexpected packet received during download." << std::endl;
            break;
        }
    }

    return true;
}

bool handleResponse(SOCKET clientSock, const ParsedPacket& response)
{
    if (response.header.command == CMD::CMD_LIST_DRIVES) {
        return handleDriveListResponse(response.payload);
    }

    if (response.header.command == CMD::CMD_LIST_DIR) {
        return handleListDirResponse(response.payload);
    }

    if (response.header.command == CMD::CMD_DOWNLOAD_START) {
        return handleDownloadStartResponse(clientSock, response.payload);
    }

    if (response.header.command == CMD::CMD_ERROR) {
        std::cerr << "Server error: " << PacketCodec::bytesToString(response.payload) << std::endl;
        return false;
    }

    std::cerr << "Unknown response command: " << response.header.command << std::endl;
    return false;
}

} // namespace

int runConsoleClient(int argc, char* argv[])
{
    std::string host = "127.0.0.1";
    unsigned short port = 12345;

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<unsigned short>(std::stoi(argv[2]));
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return -1;
    }

    SOCKET clientSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSock == INVALID_SOCKET) {
        std::cerr << "socket() failed." << std::endl;
        WSACleanup();
        return -1;
    }

    DWORD timeoutMs = 5000;
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) != 1) {
        std::cerr << "inet_pton() failed." << std::endl;
        closesocket(clientSock);
        WSACleanup();
        return -1;
    }

    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;

    if (connect(clientSock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "connect() failed." << std::endl;
        closesocket(clientSock);
        WSACleanup();
        return -1;
    }

    std::cout << "Connected to server." << std::endl;

    printHelp();

    while (true) {
        std::cout << "> ";

        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line == "quit") {
            break;
        }

        bool sent = false;
        if (line == "drives") {
            sent = sendListDrivesRequest(clientSock);
        } else if (line.rfind("dir ", 0) == 0) {
            sent = sendListDirRequest(clientSock, line.substr(4));
        } else if (line.rfind("download ", 0) == 0) {
            sent = sendDownloadStartRequest(clientSock, trimQuotes(line.substr(9)));
        } else {
            printHelp();
            continue;
        }

        if (!sent) {
            std::cerr << "Failed to send request packet." << std::endl;
            break;
        }

        ParsedPacket response{};
        if (!recvPacket(clientSock, response)) {
            std::cerr << "Failed to receive response packet. WSA error: "
                      << WSAGetLastError() << std::endl;
            break;
        }

        std::cout << "Response command: " << response.header.command << std::endl;
        std::cout << "BodyLen         : " << response.payload.size() << std::endl;

        if (!handleResponse(clientSock, response)) {
            break;
        }
    }

    closesocket(clientSock);
    WSACleanup();
    return 0;
}
