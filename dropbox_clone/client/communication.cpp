#include "communication.hpp"
#include "sync_manager.hpp"
#include "../common/packet.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <filesystem>
#include <fstream>

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

void upload_file(const std::string& full_path, const std::string& username) {
    if (!std::filesystem::exists(full_path)) {
        std::cerr << "[ERRO] Arquivo não encontrado: " << full_path << "\n";
        return;
    }

    std::string filename = std::filesystem::path(full_path).filename().string();

    std::ifstream file(full_path, std::ios::binary);
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    auto ftime = std::filesystem::last_write_time(full_path);
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(
        ftime.time_since_epoch()).count();

    std::time_t t = ts;
    std::cout << "last_write_time: " << std::put_time(std::localtime(&t), "%F %T") << "\n";

    std::string full_command = "UPLOAD\n" + filename + "\n" + std::to_string(ts) + "\n" + content;
    send_large_payload(CMD, full_command);

    std::cout << "[DEBUG] Filename: " << filename << "\n";
    std::cout << "[DEBUG] Timestamp: " << std::to_string(ts) << "\n";
    std::cout << "[DEBUG] Content length: " << content.size() << "\n";

    std::cout << "[INFO] Arquivo '" << filename << "' enviado ao servidor.\n";

    // Replica o arquivo para o diretório local do user, caso nele já não esteja
    if (!username.empty()) {
        //std::string local_sync_dir = username + "_sync_dir";
        std::string local_sync_dir =  "sync_dir";

        std::string destination = local_sync_dir + "/" + filename;

        if (!std::filesystem::equivalent(full_path, destination)) {
            try {
                std::filesystem::create_directories(local_sync_dir);
                std::ofstream out(destination, std::ios::binary);
                out << content;
                out.close();
                std::filesystem::last_write_time(destination, std::filesystem::file_time_type(std::chrono::seconds(ts)));
                std::cout << "[INFO] Arquivo também salvo em '" << destination << "'\n";
            } catch (const std::exception& e) {
                std::cerr << "[ERRO] Falha ao salvar arquivo no diretório local de sincronização: " << e.what() << "\n";
            }
        }
    }
}

void download_file(const std::string& filename) {
    std::string command = "DOWNLOAD\n" + filename;
    Packet request = make_packet(CMD, 0, 0, command.size(), command);
    send_packet(request);
    
    std::string payload = receive_full_payload();

    if (payload.rfind("UPLOAD ", 0) == 0) {
        size_t newline_pos = payload.find('\n');
        if (newline_pos != std::string::npos) {
            std::string header = payload.substr(0, newline_pos);
            std::string content = payload.substr(newline_pos + 1);
            std::string filename = header.substr(7); // após "UPLOAD "

            std::ofstream file(filename, std::ios::binary);
            file << content;
            file.close();

            std::cout << "[INFO] Arquivo '" << filename << "' salvo localmente.\n";
        } else {
            std::cerr << "[ERRO] Resposta mal formatada do servidor.\n";
        }
    } else {
        std::cerr << "[ERRO] Arquivo não encontrado no servidor.\n";
    }
}

void delete_file_on_server(const std::string& filename)
{
    // Envia comando de deleção
    std::string del_cmd = "DELETE\n" + filename;
    Packet del_pkt = make_packet(CMD, 0, 0, del_cmd.size(), del_cmd);
    send_packet(del_pkt);
}