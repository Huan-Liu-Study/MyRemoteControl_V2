#include "handlers/DriveHandler.h"

#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

#include "net/SocketHelpers.h"
#include "protocol/Command.h"
#include "protocol/Messages.h"

namespace {

std::vector<std::string> getLogicalDriveList()
{
    std::vector<std::string> drives;
    char buffer[256]{};

    DWORD size = GetLogicalDriveStringsA(sizeof(buffer), buffer);
    if (size > 0 && size <= sizeof(buffer)) {
        char* drive = buffer;
        while (*drive) {
            drives.push_back(std::string(drive));
            drive += std::strlen(drive) + 1;
        }
    }

    return drives;
}

} // namespace

bool handleListDrives(SOCKET clientSock)
{
    DriveListResponse response;
    response.drives = getLogicalDriveList();

    return sendPacket(clientSock, CMD::CMD_LIST_DRIVES, serializeDriveListResponse(response));
}
