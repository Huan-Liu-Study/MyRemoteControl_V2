#pragma once

#include <winsock2.h>

#include "protocol/PacketCodec.h"

bool handleListDirectory(SOCKET clientSock, const ByteBuffer& requestPayload);
bool handleDownloadStart(SOCKET clientSock, const ByteBuffer& requestPayload);
