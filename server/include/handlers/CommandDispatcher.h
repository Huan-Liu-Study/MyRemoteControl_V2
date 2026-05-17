#pragma once

#include "ServerSessionContext.h"
#include "protocol/PacketCodec.h"

bool dispatchCommand(ServerSessionContext& session, const ParsedPacket& request);
