#pragma once

#include <winsock2.h>

#include "protocol/PacketCodec.h"

bool handleMouseMove(SOCKET clientSock, const ByteBuffer& requestPayload);
bool handleMouseClick(SOCKET clientSock, const ByteBuffer& requestPayload);
bool handleMousePosition(SOCKET clientSock);
bool handleKeyboardEvent(SOCKET clientSock, const ByteBuffer& requestPayload);
bool handleMouseWheel(SOCKET clientSock, const ByteBuffer& requestPayload);
