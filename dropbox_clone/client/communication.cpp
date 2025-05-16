#include "communication.hpp"
#include "sync_manager.hpp"
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

void send_large_payload(uint16_t type, const std::string& payload) {
    const size_t chunk_size = PAYLOAD_SIZE;
    size_t total_size = payload.size();
    size_t total_sent = 0;
    uint16_t seqn = 0;

    std::cout << "[DEBUG] Enviando payload de tamanho total: " << total_size << "\n";

    while (total_sent < total_size) {
        Packet pkt;
        pkt.type = type;
        pkt.seqn = seqn++;
        pkt.total_size = total_size;

        size_t remaining = total_size - total_sent;
        pkt.length = (remaining < chunk_size) ? remaining : chunk_size;

        std::memcpy(pkt._payload, payload.data() + total_sent, pkt.length);

        send_packet(pkt);

        total_sent += pkt.length;
    }

    std::cout << "[DEBUG] Envio completo de " << seqn << " pacotes.\n";
}

bool recv_exact(int socket, void* buffer, size_t length) {
    size_t total_read = 0;
    while (total_read < length) {
        ssize_t n = recv(socket, (char*)buffer + total_read, length - total_read, 0);
        if (n <= 0) return false;
        total_read += n;
    }
    return true;
}

std::string receive_full_payload() {
    std::string result;
    uint32_t total_received = 0;
    uint32_t total_expected = 0;
    uint16_t current_seqn = 0;

    std::cout << "[DEBUG] Iniciando recepção de payload do servidor...\n";

    while (true) {
        Packet pkt;
        if (!recv_exact(client_socket, &pkt, sizeof(Packet))) {
            if (total_received < total_expected) {
                std::cerr << "[ERRO] Falha ao receber pacote completo.\n";
            } else {
                std::cout << "[DEBUG] Conexão encerrada após término da recepção. OK.\n";
            }
            break;
        }

        if (pkt.seqn != current_seqn) {
            std::cerr << "[ERRO] Sequência incorreta: esperado " << current_seqn << ", recebido " << pkt.seqn << "\n";
            break;
        }

        result.append(pkt._payload, pkt.length);
        total_received += pkt.length;
        total_expected = pkt.total_size;
        current_seqn++;

        if (total_received >= total_expected) {
            std::cout << "[DEBUG] Recepção completa. Tamanho total recebido: " << total_received << "\n";
            break;
        }
    }

    return result;
}