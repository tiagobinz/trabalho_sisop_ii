#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
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
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP.c_str());

    std::string local_ip = get_local_ip();
    std::string message = "PRIMARY:" + local_ip;

    int multicast_count = 0;

    while (multicast_count < MAX_SERVER_MULTICAST) {
        sendto(sock, message.c_str(), message.size(), 0, (sockaddr*)&multicast_addr, sizeof(multicast_addr));
        
        std::cout << "[P] Multicast [" << multicast_count+1 << "] enviado\n";
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
    group.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP.c_str());
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
        std::cout << "[B] Multicast recebido: " << msg << "\n";

        if (msg.find("PRIMARY:") == 0) {
            return msg.substr(8); // extrai IP após "PRIMARY:"
        }
    }
    close(sock);
    return "";
}

void send_heartbeat_to_backups(int multicast_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in multicast_addr{};
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_port = htons(multicast_port);
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP.c_str());

    std::string heartbeat_msg = "HEARTBEAT";

    while (true) {
        sendto(sock, heartbeat_msg.c_str(), heartbeat_msg.size(), 0,
               (sockaddr*)&multicast_addr, sizeof(multicast_addr));
        std::cout << "[P] Heartbeat enviado aos backups\n";
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    close(sock);
}

void listen_for_heartbeat(int port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }

    // Para socket ser nao-bloqueante
    fcntl(sock, F_SETFL, O_NONBLOCK);

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        close(sock);
        return;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    char buffer[1024];
    sockaddr_in sender_addr{};
    socklen_t addr_len = sizeof(sender_addr);

    auto last_heartbeat = std::chrono::steady_clock::now();

    while (true) {
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&sender_addr, &addr_len);
        
        if (len > 0) {
            buffer[len] = '\0';
            std::string msg(buffer);
            if (msg == "HEARTBEAT") {
                last_heartbeat = std::chrono::steady_clock::now();
                std::cout << "[B] Heartbeat recebido!\n";
            }
        }

        auto now = std::chrono::steady_clock::now();
        int elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat).count();
        if (elapsed > HEARTBEAT_DELAY) {
            std::cout << "[B] Servidor Primário falhou. Iniciando eleição...\n";

            // CHAMAR ALGORITMO DE ELEIÇÃO AQUI

            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    close(sock);
}

std::string get_local_ip() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in dummy_addr{};
    dummy_addr.sin_family = AF_INET;
    dummy_addr.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &dummy_addr.sin_addr);

    connect(sock, (sockaddr*)&dummy_addr, sizeof(dummy_addr));

    sockaddr_in local_addr{};
    socklen_t addr_len = sizeof(local_addr);
    getsockname(sock, (sockaddr*)&local_addr, &addr_len);

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_addr.sin_addr, ip_str, sizeof(ip_str));

    close(sock);
    return std::string(ip_str);
}

std::string get_client_ip(int client_socket) {
    sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    if (getpeername(client_socket, (sockaddr*)&client_addr, &addr_len) == 0) {
        char client_ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN)) {
            return std::string(client_ip);
        }
    }

    return "UNKNOWN";
}