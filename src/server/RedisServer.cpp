#include "../include/server/RedisServer.h"
#include "../include/command/RedisCommandHandler.h"
#include "../include/database/RedisDatabase.h"

#include <iostream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#endif
#include <vector>
#include <thread>
#include <cstring>
#include <signal.h>

// Global pointer for signal handling
static RedisServer* globalServer = nullptr;

void signalHandler(int signum) {
    if (globalServer) {
        std::cout << "\nCaught signal " << signum << ", shutting down...\n";
        globalServer->shutdown();
    }
    exit(signum);
}

void RedisServer::setupSignalHandler() {
    signal(SIGINT, signalHandler);
}

RedisServer::RedisServer(int port) : port(port), server_socket(-1), running(true) {
    globalServer = this;
    setupSignalHandler();
}

void RedisServer::shutdown() {
    running = false;
    if (server_socket != -1) {
        // Before shutdown, persist the database
        if (RedisDatabase::getInstance().dump("dump.my_rdb"))
            std::cout << "Database Dumped to dump.my_rdb\n";
        else 
            std::cerr << "Error dumping database\n";
#ifdef _WIN32
        closesocket(server_socket);
#else
        close(server_socket);
#endif
    }
    std::cout << "Server Shutdown Complete!\n";
}

void RedisServer::run() {
    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return;
    }
    #endif

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    #ifdef _WIN32
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Error Creating Server Socket\n";
        WSACleanup();
        return;
    }
    #else
    if (server_socket < 0) {
        std::cerr << "Error Creating Server Socket\n";
        return;
    }
    #endif

    int opt = 1;
    #ifdef _WIN32
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    #else
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #endif

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    #ifdef _WIN32
    if (bind(server_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Error Binding Server Socket\n";
        closesocket(server_socket);
        WSACleanup();
        return;
    }
    if (listen(server_socket, 10) == SOCKET_ERROR) {
        std::cerr << "Error Listening On Server Socket\n";
        closesocket(server_socket);
        WSACleanup();
        return;
    }
    #else
    if (bind(server_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error Binding Server Socket\n";
        close(server_socket);
        return;
    }
    if (listen(server_socket, 10) < 0) {
        std::cerr << "Error Listening On Server Socket\n";
        close(server_socket);
        return;
    }
    #endif

    std::cout << "Redis Server Listening On Port " << port << "\n";

    std::vector<std::thread> threads;
    RedisCommandHandler cmdHandler;

    while (running) {
        #ifdef _WIN32
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            if (running)
                std::cerr << "Error Accepting Client Connection\n";
            break;
        }
        #else
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket < 0) {
            if (running)
                std::cerr << "Error Accepting Client Connection\n";
            break;
        }
        #endif

        threads.emplace_back([client_socket, &cmdHandler](){
            char buffer[1024];
            while (true) {
                memset(buffer, 0, sizeof(buffer));
                #ifdef _WIN32
                int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                #else
                int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                #endif
                if (bytes <= 0) break;
                std::string request(buffer, bytes);
                std::string response = cmdHandler.processCommand(request);
                send(client_socket, response.c_str(), response.size(), 0);
            }
#ifdef _WIN32
            closesocket(client_socket);
#else
            close(client_socket);
#endif
        });
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    // Before shutdown, persist the database
    if (RedisDatabase::getInstance().dump("dump.my_rdb"))
        std::cout << "Database Dumped to dump.my_rdb\n";
    else 
        std::cerr << "Error dumping database\n";

    #ifdef _WIN32
    WSACleanup();
    #endif
}
