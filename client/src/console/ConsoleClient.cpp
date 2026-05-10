#include "console/ConsoleClient.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/RemoteClientCore.h"
#include "protocol/Messages.h"

namespace {

void printHelp()
{
    std::cout << "Commands:" << std::endl;
    std::cout << "  drives          list remote drives" << std::endl;
    std::cout << "  dir <path>      list remote directory, example: dir C:\\" << std::endl;
    std::cout << "  download <path> download remote small file" << std::endl;
    std::cout << "  screenshot      save remote screen as screenshot.bmp" << std::endl;
    std::cout << "  mouse move <x> <y>       move remote mouse to absolute screen position" << std::endl;
    std::cout << "  mouse click <left|right|middle>  click remote mouse button" << std::endl;
    std::cout << "  mouse down <left|right|middle>   press remote mouse button" << std::endl;
    std::cout << "  mouse up <left|right|middle>     release remote mouse button" << std::endl;
    std::cout << "  mouse drag <left|right|middle> <fromX> <fromY> <toX> <toY>" << std::endl;
    std::cout << "  mouse smoothdrag <left|right|middle> <fromX> <fromY> <toX> <toY> <steps>" << std::endl;
    std::cout << "  mouse wheel <delta>      scroll remote mouse wheel, example: mouse wheel -120" << std::endl;
    std::cout << "  mouse pos                show remote mouse position" << std::endl;
    std::cout << "  quit            exit client" << std::endl;
}

std::string trimQuotes(const std::string& text)
{
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return text.substr(1, text.size() - 2);
    }

    return text;
}

bool printDriveList(const std::vector<std::string>& drives)
{
    for (const std::string& drive : drives) {
        std::cout << "Drive: " << drive << std::endl;
    }

    return true;
}

bool printDirectoryList(const std::vector<FileEntry>& entries)
{
    for (const FileEntry& entry : entries) {
        std::cout << (entry.isDirectory ? "[DIR]  " : "[FILE] ");
        std::cout << entry.name << std::endl;
    }

    return true;
}

bool downloadFile(RemoteClientCore& client, const std::string& remotePath)
{
    std::string errorMessage;
    DownloadStartResponse decoded{};
    if (!client.requestDownload(remotePath, decoded, errorMessage)) {
        std::cerr << errorMessage << std::endl;
        return false;
    }

    if (!decoded.ok) {
        std::cerr << "Server denied download: " << decoded.errorMessage << std::endl;
        return true;
    }

    std::string localFileName = decoded.fileName.empty() ? "download.bin" : decoded.fileName;
    std::ofstream out(localFileName, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to create local file: " << localFileName << std::endl;
        return true;
    }

    std::cout << "\n[Downloading] " << localFileName
              << " (Size: " << decoded.fileSize << " bytes)\n" << std::endl;

    uint64_t totalReceived = 0;
    const bool ok = client.receiveDownloadChunks(
        [&](const ByteBuffer& chunk, std::string& chunkError) {
            out.write(reinterpret_cast<const char*>(chunk.data()), chunk.size());
            if (!out) {
                chunkError = "Failed to write local file.";
                return false;
            }

            totalReceived += chunk.size();
            std::cout << "\rProgress: " << totalReceived << " / "
                      << decoded.fileSize << " bytes" << std::flush;
            return true;
        },
        errorMessage
    );

    if (!ok) {
        std::cerr << "\n[!] " << errorMessage << std::endl;
        return false;
    }

    std::cout << "\n\nDownload complete." << std::endl;
    return true;
}

bool saveScreenshot(RemoteClientCore& client)
{
    std::string errorMessage;
    ScreenshotStartResponse response{};
    if (!client.requestScreenshot(response, errorMessage)) {
        std::cerr << errorMessage << std::endl;
        return false;
    }

    if (!response.ok) {
        std::cerr << "Server denied screenshot: " << response.errorMessage << std::endl;
        return true;
    }

    std::string localFileName = response.fileName.empty() ? "screenshot.bmp" : response.fileName;
    std::ofstream out(localFileName, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to create local file: " << localFileName << std::endl;
        return true;
    }

    std::cout << "\n[Receiving screenshot] " << localFileName
              << " (Size: " << response.imageSize << " bytes)\n" << std::endl;

    uint64_t totalReceived = 0;
    const bool ok = client.receiveScreenshotChunks(
        [&](const ByteBuffer& chunk, std::string& chunkError) {
            out.write(reinterpret_cast<const char*>(chunk.data()), chunk.size());
            if (!out) {
                chunkError = "Failed to write local screenshot.";
                return false;
            }

            totalReceived += chunk.size();
            std::cout << "\rProgress: " << totalReceived << " / "
                      << response.imageSize << " bytes" << std::flush;
            return true;
        },
        errorMessage
    );

    if (!ok) {
        std::cerr << "\n[!] " << errorMessage << std::endl;
        return false;
    }

    std::cout << "\n\nScreenshot saved." << std::endl;
    return true;
}

uint32_t parseMouseButton(const std::string& button)
{
    if (button == "left") {
        return 1;
    }

    if (button == "right") {
        return 2;
    }

    if (button == "middle") {
        return 3;
    }

    return 0;
}

uint32_t parseMouseAction(const std::string& action)
{
    if (action == "down") {
        return 1;
    }

    if (action == "up") {
        return 2;
    }

    if (action == "click") {
        return 3;
    }

    return 0;
}

bool handleMouseCommand(RemoteClientCore& client, const std::string& line)
{
    std::istringstream input(line);

    std::string mouse;
    std::string action;
    input >> mouse >> action;

    std::string errorMessage;

    if (action == "pos") {
        MousePositionResponse position{};
        if (!client.getMousePosition(position, errorMessage)) {
            std::cerr << errorMessage << std::endl;
            return false;
        }

        std::cout << "Mouse position: " << position.x << ", " << position.y << std::endl;
        return true;
    }

    if (action == "move") {
        int32_t x = 0;
        int32_t y = 0;
        if (!(input >> x >> y)) {
            std::cerr << "Usage: mouse move <x> <y>" << std::endl;
            return true;
        }

        if (!client.moveMouse(x, y, errorMessage)) {
            std::cerr << errorMessage << std::endl;
            return false;
        }

        std::cout << "Mouse moved to " << x << ", " << y << std::endl;
        return true;
    }

    if (action == "wheel") {
        int32_t delta = 0;
        if (!(input >> delta)) {
            std::cerr << "Usage: mouse wheel <delta>" << std::endl;
            return true;
        }

        if (!client.sendMouseWheel(delta, errorMessage)) {
            std::cerr << errorMessage << std::endl;
            return false;
        }

        std::cout << "Mouse wheel: " << delta << std::endl;
        return true;
    }

    if (action == "drag") {
        std::string buttonText;
        int32_t fromX = 0;
        int32_t fromY = 0;
        int32_t toX = 0;
        int32_t toY = 0;
        input >> buttonText >> fromX >> fromY >> toX >> toY;

        const uint32_t button = parseMouseButton(buttonText);
        if (button == 0 || !input) {
            std::cerr << "Usage: mouse drag <left|right|middle> <fromX> <fromY> <toX> <toY>" << std::endl;
            return true;
        }

        if (!client.dragMouse(button, fromX, fromY, toX, toY, errorMessage)) {
            std::cerr << errorMessage << std::endl;
            return false;
        }

        std::cout << "Mouse dragged: " << buttonText
                  << " from " << fromX << ", " << fromY
                  << " to " << toX << ", " << toY << std::endl;
        return true;
    }

    if (action == "smoothdrag") {
        std::string buttonText;
        int32_t fromX = 0;
        int32_t fromY = 0;
        int32_t toX = 0;
        int32_t toY = 0;
        uint32_t steps = 0;
        input >> buttonText >> fromX >> fromY >> toX >> toY >> steps;

        const uint32_t button = parseMouseButton(buttonText);
        if (button == 0 || !input) {
            std::cerr << "Usage: mouse smoothdrag <left|right|middle> <fromX> <fromY> <toX> <toY> <steps>" << std::endl;
            return true;
        }

        if (!client.smoothDragMouse(button, fromX, fromY, toX, toY, steps, errorMessage)) {
            std::cerr << errorMessage << std::endl;
            return false;
        }

        std::cout << "Mouse smooth dragged: " << buttonText
                  << " from " << fromX << ", " << fromY
                  << " to " << toX << ", " << toY
                  << " in " << steps << " steps" << std::endl;
        return true;
    }

    if (action == "down" || action == "up" || action == "click") {
        std::string buttonText;
        input >> buttonText;

        const uint32_t button = parseMouseButton(buttonText);
        if (button == 0) {
            std::cerr << "Usage: mouse <down|up|click> <left|right|middle>" << std::endl;
            return true;
        }

        const uint32_t mouseAction = parseMouseAction(action);
        if (!client.sendMouseButton(button, mouseAction, errorMessage)) {
            std::cerr << errorMessage << std::endl;
            return false;
        }

        std::cout << "Mouse " << action << ": " << buttonText << std::endl;
        return true;
    }

    std::cerr << "Usage: mouse move <x> <y>, mouse wheel <delta>, mouse pos, mouse drag/smoothdrag <button> <fromX> <fromY> <toX> <toY>, or mouse <down|up|click> <button>" << std::endl;
    return true;
}

bool handleCommand(RemoteClientCore& client, const std::string& line)
{
    std::string errorMessage;

    if (line == "drives") {
        std::vector<std::string> drives;
        if (!client.listDrives(drives, errorMessage)) {
            std::cerr << errorMessage << std::endl;
            return false;
        }

        return printDriveList(drives);
    }

    if (line.rfind("dir ", 0) == 0) {
        std::vector<FileEntry> entries;
        if (!client.listDirectory(line.substr(4), entries, errorMessage)) {
            std::cerr << errorMessage << std::endl;
            return false;
        }

        return printDirectoryList(entries);
    }

    if (line.rfind("download ", 0) == 0) {
        return downloadFile(client, trimQuotes(line.substr(9)));
    }

    if (line == "screenshot") {
        return saveScreenshot(client);
    }

    if (line.rfind("mouse ", 0) == 0) {
        return handleMouseCommand(client, line);
    }

    printHelp();
    return true;
}

} // namespace

int runConsoleClient(int argc, char* argv[])
{
    std::string host = "127.0.0.1";
    unsigned short port = 12345;

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<unsigned short>(std::stoi(argv[2]));
    }

    RemoteClientCore client;

    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;

    std::string errorMessage;
    if (!client.connectToServer(host, port, errorMessage)) {
        std::cerr << errorMessage << std::endl;
        return -1;
    }

    std::cout << "Connected to server." << std::endl;

    printHelp();

    while (true) {
        std::cout << "> ";

        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line == "quit") {
            break;
        }

        if (!handleCommand(client, line)) {
            break;
        }
    }

    client.disconnect();
    return 0;
}
