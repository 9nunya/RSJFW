#include "rsjfw/socket.hpp"
#include "rsjfw/logger.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include <iostream>
#include <vector>
#include <sstream>
#include <cstring>

namespace rsjfw {

SingleInstance::SingleInstance(const std::string& name) {
    socketPath_ = "/tmp/rsjfw_" + name + ".sock";
}

SingleInstance::~SingleInstance() {
    stop();
}

bool SingleInstance::isPrimary() {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        LOG_ERROR("Failed to create socket");
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != -1) {
            close(sock);
            return false;
        } else {
            unlink(socketPath_.c_str());
            if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
                LOG_ERROR("Failed to bind socket even after unlink");
                close(sock);
                return false;
            }
        }
    }

    if (::listen(sock, 5) == -1) {
        LOG_ERROR("Failed to listen on socket");
        close(sock);
        return false;
    }

    serverSocket_ = sock;
    isPrimary_ = true;
    return true;
}

void SingleInstance::sendArgs(const std::vector<std::string>& args) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(sock);
        return;
    }

    std::string payload;
    for (const auto& arg : args) {
        payload += arg + "\n";
    }

    send(sock, payload.c_str(), payload.size(), 0);
    close(sock);
}

void SingleInstance::listen(std::function<void(const std::vector<std::string>&)> callback) {
    if (!isPrimary_ || serverSocket_ == -1) return;
    
    running_ = true;
    std::thread([this, callback]() {
        while (running_) {
            int clientSock = accept(serverSocket_, NULL, NULL);
            if (clientSock == -1) continue;

            if (!running_) {
                close(clientSock);
                break;
            }

            std::string payload;
            char buffer[1024];
            while (true) {
                ssize_t bytes = recv(clientSock, buffer, sizeof(buffer), 0);
                if (bytes <= 0) break;
                payload.append(buffer, bytes);
            }
            close(clientSock);

            if (!payload.empty()) {
                std::vector<std::string> args;
                std::stringstream ss(payload);
                std::string arg;
                while (std::getline(ss, arg, '\n')) {
                    args.push_back(arg);
                }
                
                if (!args.empty()) {
                    callback(args);
                }
            }
        }
    }).detach();
}

void SingleInstance::stop() {
    running_ = false;
    if (serverSocket_ != -1) {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock != -1) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);
            connect(sock, (struct sockaddr*)&addr, sizeof(addr));
            close(sock);
        }

        close(serverSocket_);
        serverSocket_ = -1;
        unlink(socketPath_.c_str());
    }
}

} // namespace rsjfw
