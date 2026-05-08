#include "handlers/FileHandler.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

#include "net/SocketHelpers.h"
#include "protocol/Command.h"
#include "protocol/Messages.h"

namespace {

std::vector<FileEntry> getDirectoryEntries(const std::string& path)
{
    std::vector<FileEntry> entries;

    std::string searchPath = path;
    if (searchPath.empty()) {
        return entries;
    }

    if (searchPath.back() != '\\' && searchPath.back() != '/') {
        searchPath += "\\";
    }
    searchPath += "*";

    WIN32_FIND_DATAA findData{};
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return entries;
    }

    do {
        std::string name = findData.cFileName;
        if (name == "." || name == "..") {
            continue;
        }

        FileEntry entry{};
        entry.name = std::move(name);
        entry.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1u : 0u;
        entries.push_back(std::move(entry));
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    return entries;
}

bool sendDownloadStartError(SOCKET clientSock, const std::string& message)
{
    DownloadStartResponse response{};
    response.ok = 0;
    response.fileSize = 0;
    response.errorMessage = message;

    return sendPacket(clientSock, CMD::CMD_DOWNLOAD_START, serializeDownloadStartResponse(response));
}

} // namespace

bool handleListDirectory(SOCKET clientSock, const ByteBuffer& requestPayload)
{
    ListDirRequest request{};
    if (!deserializeListDirRequest(requestPayload, request)) {
        return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid list directory request.");
    }

    ListDirResponse response{};
    response.entries = getDirectoryEntries(request.path);

    return sendPacket(clientSock, CMD::CMD_LIST_DIR, serializeListDirResponse(response));
}

bool handleDownloadStart(SOCKET clientSock, const ByteBuffer& requestPayload)
{
    DownloadStartRequest request{};
    if (!deserializeDownloadStartRequest(requestPayload, request)) {
        return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid download start request.");
    }

    if (request.path.empty()) {
        return sendDownloadStartError(clientSock, "Path is empty.");
    }

    std::cout << "Download path: " << request.path << std::endl;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(request.path, ec)) {
        return sendDownloadStartError(clientSock, "File not found or access denied.");
    }

    uintmax_t fileSize = std::filesystem::file_size(request.path, ec);
    if (ec) {
        return sendDownloadStartError(clientSock, "Failed to get file size.");
    }

    std::ifstream file(request.path, std::ios::binary);
    if (!file) {
        return sendDownloadStartError(clientSock, "Failed to open file for reading.");
    }

    DownloadStartResponse response{};
    response.ok = 1;
    response.fileSize = static_cast<uint64_t>(fileSize);
    response.fileName = std::filesystem::path(request.path).filename().string();

    if (!sendPacket(clientSock, CMD::CMD_DOWNLOAD_START, serializeDownloadStartResponse(response))) {
        return false;
    }

    std::cout << "[File Transfer] Starting chunked streaming. Total Size: "
              << fileSize << " bytes" << std::endl;

    constexpr size_t chunkSize = 65536;
    std::vector<uint8_t> buffer(chunkSize);

    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), chunkSize);
        std::streamsize bytesRead = file.gcount();

        if (bytesRead > 0) {
            ByteBuffer chunkPayload(buffer.begin(), buffer.begin() + bytesRead);
            if (!sendPacket(clientSock, CMD::CMD_DOWNLOAD_CHUNK, chunkPayload)) {
                std::cerr << "[File Transfer] Transfer interrupted by network error." << std::endl;
                return false;
            }
        }
    }

    std::cout << "[File Transfer] Complete. Sending END signal." << std::endl;
    return sendPacket(clientSock, CMD::CMD_DOWNLOAD_END, ByteBuffer{});
}
