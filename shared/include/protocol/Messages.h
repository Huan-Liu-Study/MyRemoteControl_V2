#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "protocol/PacketCodec.h"

struct DriveListResponse {
    std::vector<std::string> drives;
};

struct ListDirRequest {
    std::string path;
};

struct FileEntry {
    std::string name;
    uint32_t isDirectory;
};

struct ListDirResponse {
    std::vector<FileEntry> entries;
};

struct DownloadStartRequest {
    std::string path;
};

struct DownloadStartResponse {
    uint32_t ok;
    uint64_t fileSize;
    std::string fileName;
    std::string errorMessage;
};



ByteBuffer serializeDriveListResponse(const DriveListResponse& response);
bool deserializeDriveListResponse(const ByteBuffer& payload, DriveListResponse& outResponse);

ByteBuffer serializeListDirRequest(const ListDirRequest& request);
bool deserializeListDirRequest(const ByteBuffer& payload, ListDirRequest& outRequest);

ByteBuffer serializeListDirResponse(const ListDirResponse& response);
bool deserializeListDirResponse(const ByteBuffer& payload, ListDirResponse& outResponse);

ByteBuffer serializeDownloadStartRequest(const DownloadStartRequest& request);
bool deserializeDownloadStartRequest(const ByteBuffer& payload, DownloadStartRequest& outRequest);

ByteBuffer serializeDownloadStartResponse(const DownloadStartResponse& response);
bool deserializeDownloadStartResponse(const ByteBuffer& payload, DownloadStartResponse& outResponse);
