#pragma once

#include <cstdint>

class IocpServer {
public:
    explicit IocpServer(uint16_t port);
    ~IocpServer();

    bool start();
    void run();
    void stop();

private:
    struct Impl;
    Impl* impl_;
};
