#pragma once

#include <winsock2.h>

#include "protocol/PacketCodec.h"

bool handleScreenshotStart(SOCKET clientSock, const ByteBuffer& requestPayload);
bool handleScreenStreamStart(SOCKET clientSock, const ByteBuffer& requestPayload);
