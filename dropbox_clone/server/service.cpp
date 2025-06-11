#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "service.hpp"

void multicast_primary_info(int multicast_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    // Permitir reuso de endereço
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in multicast_addr{};
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_port = htons(multicast_port);
    multicast_addr.sin_addr.s_addr = inet_addr("239.0.0.1"); // grupo multicast

    std::string local_ip = get_local_ip();
    std::string message = "PRIMARY:" + local_ip;

    int multicast_count = 0;

    while (multicast_count <= MAX_SERVER_MULTICAST) {
        sendto(sock, message.c_str(), message.size(), 0,
               (sockaddr*)&multicast_addr, sizeof(multicast_addr));
        std::cout << "[P] Multicast enviado\n";
        std::this_thread::sleep_for(std::chrono::seconds(3));
        multicast_count++;
    }

    close(sock);
}


std::string listen_for_primary_multicast(int multicast_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    // Permitir reuso de endereço
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(multicast_port);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        std::cerr << "[ERRO] Bind falhou no socket multicast.\n";
        close(sock);
        return "";
    }

    // Entrar no grupo multicast
    ip_mreq group{};
    group.imr_multiaddr.s_addr = inet_addr("239.0.0.1");
    group.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&group, sizeof(group)) < 0) {
        std::cerr << "[ERRO] Falha ao entrar no grupo multicast.\n";
        close(sock);
        return "";
    }

    char buffer[1024];
    while (true) {
        ssize_t received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (received < 0) continue;

        buffer[received] = '\0';
        std::string msg(buffer);
        std::cout << "[B] Recebido do multicast: " << msg << "\n";

        if (msg.find("PRIMARY:") == 0) {
            return msg.substr(8); // extrai IP após "PRIMARY:"
        }
    }

    close(sock);
    return "";
}

std::string get_local_ip() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in dummy_addr{};
    dummy_addr.sin_family = AF_INET;
    dummy_addr.sin_port = htons(80); // qualquer porta válida
    inet_pton(AF_INET, "8.8.8.8", &dummy_addr.sin_addr); // usar Google DNS só para simular

    connect(sock, (sockaddr*)&dummy_addr, sizeof(dummy_addr)); // não precisa realmente conectar

    sockaddr_in local_addr{};
    socklen_t addr_len = sizeof(local_addr);
    getsockname(sock, (sockaddr*)&local_addr, &addr_len);

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_addr.sin_addr, ip_str, sizeof(ip_str));

    close(sock);
    return std::string(ip_str);
}

