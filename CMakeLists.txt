# 1. 声明要求的最低 CMake 版本
cmake_minimum_required(VERSION 3.15)
# 2. 定义工程名称和编程语言（CXX 代表 C++）
project(MyRemoteControl_V2 CXX)

# 3. 设置 C++ 标准为 C++17 (非常重要：我们将用许多现代并发和文件系统特性)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

link_libraries(ws2_32 gdi32 gdiplus)

include_directories(shared/include)
include_directories(server/include)
include_directories(client/include)

set(SHARED_SOURCES
    shared/src/net/SocketHelpers.cpp
    shared/src/protocol/BinaryReader.cpp
    shared/src/protocol/BinaryWriter.cpp
    shared/src/protocol/Messages.cpp
    shared/src/protocol/PacketCodec.cpp
)

add_executable(RemoteServer
    server/src/main.cpp
    server/src/ServerSession.cpp
    server/src/net/IocpServer.cpp
    server/src/handlers/CommandDispatcher.cpp
    server/src/handlers/DriveHandler.cpp
    server/src/handlers/FileHandler.cpp
    server/src/handlers/InputHandler.cpp
    server/src/handlers/ScreenHandler.cpp
    ${SHARED_SOURCES}
)

add_executable(RemoteClient
    client/src/console/main.cpp
    client/src/console/ConsoleClient.cpp
    client/src/core/RemoteClientCore.cpp
    ${SHARED_SOURCES}
)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)

    add_executable(RemoteClient_App
        client/src/qt/qt_main.cpp
        client/src/qt/MainWindow.cpp
        client/src/qt/RemoteClientWorker.cpp
        client/src/qt/ScreenClientWorker.cpp
        client/src/core/RemoteClientCore.cpp
        client/include/qt/MainWindow.h
        client/include/qt/RemoteClientWorker.h
        client/include/qt/ScreenClientWorker.h
        client/include/core/RemoteClientCore.h
        ${SHARED_SOURCES}
    )

    target_link_libraries(RemoteClient_App PRIVATE Qt6::Widgets ws2_32)
else()
    message(STATUS "Qt6 Widgets was not found, skip RemoteClient_App.")
endif()

enable_testing()

add_executable(BinaryReaderWriterTests
    tests/BinaryReaderWriterTests.cpp
    shared/src/protocol/BinaryReader.cpp
    shared/src/protocol/BinaryWriter.cpp
)

add_test(NAME BinaryReaderWriterTests COMMAND BinaryReaderWriterTests)

add_executable(MessageSerializationTests
    tests/MessageSerializationTests.cpp
    shared/src/protocol/BinaryReader.cpp
    shared/src/protocol/BinaryWriter.cpp
    shared/src/protocol/Messages.cpp
)

add_test(NAME MessageSerializationTests COMMAND MessageSerializationTests)

add_executable(ThreadPoolFutureTests
    tests/ThreadPoolFutureTests.cpp
)

add_test(NAME ThreadPoolFutureTests COMMAND ThreadPoolFutureTests)
