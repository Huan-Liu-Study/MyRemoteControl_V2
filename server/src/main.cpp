#include <iostream>
#include <windows.h>

#include "net/IocpServer.h"

namespace {

void enableDpiAwareCoordinateSystem()
{
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

    HMODULE user32 = GetModuleHandleA("user32.dll");
    auto setProcessDpiAwarenessContext = user32
        ? reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext")
        )
        : nullptr;

    if (setProcessDpiAwarenessContext
        && setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        return;
    }

    SetProcessDPIAware();
}

} // namespace

int main() {
    enableDpiAwareCoordinateSystem();

    std::cout << "RemoteServer protocol build: packet-v2-iocp" << std::endl;

    IocpServer server(12345);
    if (!server.start()) {
        return -1;
    }

    server.run();
    return 0;
}
