#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include "election.hpp"

std::string bully_election(std::unordered_map<int, std::string> backups) {

    info.election_started = true;
    int this_id = info.election_id;

    std::string election_msg = "ELECTION|" + std::to_string(this_id);

    // Enviar ELECTION para todos os backups com id menor
    for (const auto& [backup_id, ip] : info.backups) {
        if(this_id < backup_id) {

            std::cout << "[ELECTION] Enviando ELECTION para backup em: " << ip << "\n";
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                perror("[ERRO] socket");
                continue;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(ELECTION_PORT);
            inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

            if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
                std::cerr << "[ELECTION] Falha ao conectar com " << ip << "\n";
                close(sock);
                continue;
            }

            send(sock, election_msg.c_str(), election_msg.size(), 0);

            close(sock);
        }
    }

    return "";
}