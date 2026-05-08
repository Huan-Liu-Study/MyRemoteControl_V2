#include "protocol/Messages.h"

#include <utility>

#include "protocol/BinaryReader.h"
#include "protocol/BinaryWriter.h"

ByteBuffer serializeDriveListResponse(const DriveListResponse& response)
{
    BinaryWriter writer;
    writer.writeUint32(static_cast<uint32_t>(response.drives.size()));

    for (const std::string& drive : response.drives) {
        writer.writeString(drive);
    }

    return writer.buffer();
}

bool deserializeDriveListResponse(const ByteBuffer& payload, DriveListResponse& outResponse)
{
    BinaryReader reader(payload);

    uint32_t driveCount = 0;
    if (!reader.readUint32(driveCount)) {
        return false;
    }

    std::vector<std::string> drives;
    drives.reserve(driveCount);

    for (uint32_t i = 0; i < driveCount; ++i) {
        std::string drive;
        if (!reader.readString(drive)) {
            return false;
        }

        drives.push_back(std::move(drive));
    }

    if (!reader.isFinished()) {
        return false;
    }

    outResponse.drives = std::move(drives);
    return true;
}

ByteBuffer serializeListDirRequest(const ListDirRequest& request)
{
    BinaryWriter writer;
    writer.writeString(request.path);
    return writer.buffer();
}

bool deserializeListDirRequest(const ByteBuffer& payload, ListDirRequest& outRequest)
{
    BinaryReader reader(payload);

    std::string path;
    if (!reader.readString(path)) {
        return false;
    }

    if (!reader.isFinished()) {
        return false;
    }

    outRequest.path = std::move(path);
    return true;
}

ByteBuffer serializeListDirResponse(const ListDirResponse& response)
{
    BinaryWriter writer;
    writer.writeUint32(static_cast<uint32_t>(response.entries.size()));

    for (const FileEntry& entry : response.entries) {
        writer.writeString(entry.name);
        writer.writeUint32(entry.isDirectory);
    }

    return writer.buffer();
}

bool deserializeListDirResponse(const ByteBuffer& payload, ListDirResponse& outResponse)
{
    BinaryReader reader(payload);

    uint32_t entryCount = 0;
    if (!reader.readUint32(entryCount)) {
        return false;
    }

    std::vector<FileEntry> entries;
    entries.reserve(entryCount);

    for (uint32_t i = 0; i < entryCount; ++i) {
        FileEntry entry{};

        if (!reader.readString(entry.name) || !reader.readUint32(entry.isDirectory)) {
            return false;
        }

        entries.push_back(std::move(entry));
    }

    if (!reader.isFinished()) {
        return false;
    }

    outResponse.entries = std::move(entries);
    return true;
}

ByteBuffer serializeDownloadStartRequest(const DownloadStartRequest& request)
{
    BinaryWriter writer;
    writer.writeString(request.path);
    return writer.buffer();
}

bool deserializeDownloadStartRequest(const ByteBuffer& payload, DownloadStartRequest& outRequest)
{
    BinaryReader reader(payload);

    std::string path;
    if (!reader.readString(path)) {
        return false;
    }

    if (!reader.isFinished()) {
        return false;
    }

    outRequest.path = std::move(path);
    return true;
}

ByteBuffer serializeDownloadStartResponse(const DownloadStartResponse& response)
{
    BinaryWriter writer;
    writer.writeUint32(response.ok);
    writer.writeUint64(response.fileSize);
    writer.writeString(response.fileName);
    writer.writeString(response.errorMessage);
    return writer.buffer();
}

bool deserializeDownloadStartResponse(const ByteBuffer& payload, DownloadStartResponse& outResponse)
{
    BinaryReader reader(payload);

    if (!reader.readUint32(outResponse.ok)) {
        return false;
    }

    if (!reader.readUint64(outResponse.fileSize)) {
        return false;
    }

    if (!reader.readString(outResponse.fileName) || !reader.readString(outResponse.errorMessage)) {
        return false;
    }

    return reader.isFinished();
}
