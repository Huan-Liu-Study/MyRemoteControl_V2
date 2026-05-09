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

std::wstring utf8ToWide(const std::string& text)
{
    if (text.empty()) {
        return {};
    }

    int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (size <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size
    );
    return result;
}

std::string wideToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }

    int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr
    );
    return result;
}

std::vector<FileEntry> getDirectoryEntries(const std::wstring& path)
{
    std::vector<FileEntry> entries;

    std::wstring searchPath = path;
    if (searchPath.empty()) {
        return entries;
    }

    if (searchPath.back() != L'\\' && searchPath.back() != L'/') {
        searchPath += L"\\";
    }
    searchPath += L"*";

    WIN32_FIND_DATAW findData{};
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return entries;
    }

    do {
        std::wstring name = findData.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }

        FileEntry entry{};
        entry.name = wideToUtf8(name);
        entry.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1u : 0u;
        entries.push_back(std::move(entry));
    } while (FindNextFileW(hFind, &findData));

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

    std::wstring path = utf8ToWide(request.path);
    if (path.empty()) {
        return sendPacket(clientSock, CMD::CMD_ERROR, "Invalid UTF-8 directory path.");
    }

    ListDirResponse response{};
    response.entries = getDirectoryEntries(path);

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

    std::wstring widePath = utf8ToWide(request.path);
    if (widePath.empty()) {
        return sendDownloadStartError(clientSock, "Invalid UTF-8 file path.");
    }

    std::cout << "Download path: " << request.path << std::endl;

    std::filesystem::path filePath(widePath);

    std::error_code ec;
    if (!std::filesystem::is_regular_file(filePath, ec)) {
        return sendDownloadStartError(clientSock, "File not found or access denied.");
    }

    uintmax_t fileSize = std::filesystem::file_size(filePath, ec);
    if (ec) {
        return sendDownloadStartError(clientSock, "Failed to get file size.");
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return sendDownloadStartError(clientSock, "Failed to open file for reading.");
    }

    DownloadStartResponse response{};
    response.ok = 1;
    response.fileSize = static_cast<uint64_t>(fileSize);
    response.fileName = wideToUtf8(filePath.filename().wstring());

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
