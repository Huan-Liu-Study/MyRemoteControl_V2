#pragma once

#include <string>
#include <winsock2.h>

#include "protocol/PacketCodec.h"

bool sendAll(SOCKET sock, const char* buffer, int totalBytes);

bool recvAll(SOCKET sock, char* buffer, int totalBytes);

bool sendPacket(SOCKET sock, CMD::Type command, const ByteBuffer& payload = {});

bool sendPacket(SOCKET sock, CMD::Type command, const std::string& text);

bool recvPacket(SOCKET sock, ParsedPacket& outPacket);
