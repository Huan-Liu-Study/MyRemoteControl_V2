#include "net/SocketHelpers.h"

#include <climits>

bool sendAll(SOCKET sock, const char* buffer, int totalBytes)
{
    int sent = 0;

    while (sent < totalBytes) {
        int result = send(sock, buffer + sent, totalBytes - sent, 0);
        if (result <= 0) {
            return false;
        }

        sent += result;
    }

    return true;
}

bool recvAll(SOCKET sock, char* buffer, int totalBytes)
{
    int received = 0;

    while (received < totalBytes) {
        int result = recv(sock, buffer + received, totalBytes - received, 0);
        if (result <= 0) {
            return false;
        }

        received += result;
    }

    return true;
}

bool sendPacket(SOCKET sock, CMD::Type command, const ByteBuffer& payload)
{
    if (payload.size() > MAX_BODY_SIZE) {
        return false;
    }

    ByteBuffer packet = PacketCodec::pack(command, payload);
    if (packet.size() > static_cast<size_t>(INT_MAX)) {
        return false;
    }

    return sendAll(sock, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()));
}

bool sendPacket(SOCKET sock, CMD::Type command, const std::string& text)
{
    return sendPacket(sock, command, PacketCodec::stringToBytes(text));
}

bool recvPacket(SOCKET sock, ParsedPacket& outPacket)
{
    PacketHeader header{};
    if (!recvAll(sock, reinterpret_cast<char*>(&header), static_cast<int>(PACKET_HEADER_SIZE))) {
        return false;
    }

    if (header.length < static_cast<uint32_t>(PACKET_HEADER_SIZE)) {
        return false;
    }

    uint32_t bodyLen = header.length - static_cast<uint32_t>(PACKET_HEADER_SIZE);

    if (bodyLen > MAX_BODY_SIZE) {
        return false;
    }

    if (bodyLen > static_cast<uint32_t>(INT_MAX)) {
        return false;
    }

    ByteBuffer body(bodyLen);

    if (bodyLen > 0) {
        if (!recvAll(sock, reinterpret_cast<char*>(body.data()), static_cast<int>(bodyLen))) {
            return false;
        }
    }

    return PacketCodec::unpack(header, body, outPacket);
}
