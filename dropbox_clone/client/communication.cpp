#include "communication.hpp"
#include "../common/packet.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

int client_socket;

bool connect_to_server(const std::string& ip, int port) {
    // Criação do socket TCP
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        std::cerr << "[ERRO] Falha ao criar socket.\n";
        return false;
    }

    // Preenchimento da estrutura do endereço do servidor
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Converte o IP string para binário
    int pton_result = inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
    if (pton_result <= 0) {
        std::cerr << "[ERRO] inet_pton falhou. IP inválido?\n";
        close(client_socket);
        return false;
    }

    // Tentativa de conexão
    int conn_result = connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    if (conn_result < 0) {
        std::cerr << "[ERRO] Falha ao conectar-se ao servidor.\n";
        close(client_socket);
        return false;
    }

    return true;
}

void send_packet(const Packet& pkt) {
    send(client_socket, &pkt, sizeof(Packet), 0);
}

Packet receive_packet() {
    Packet pkt;
    recv(client_socket, &pkt, sizeof(Packet), 0);
    return pkt;
}
