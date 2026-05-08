#pragma once

#include <winsock2.h>

#include "protocol/PacketCodec.h"

bool dispatchCommand(SOCKET clientSock, const ParsedPacket& request);
