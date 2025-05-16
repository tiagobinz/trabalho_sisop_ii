#include "communication.hpp"
#include "../common/packet.hpp"
#include "../common/utils.hpp"
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <algorithm>

// Registrar os sockets ativos por usuário
std::unordered_map<std::string, std::vector<int>> user_sockets;
std::mutex socket_mutex;

// Controle do número máximo de dispositivos conectados
std::unordered_map<std::string, int> user_session_count;
std::mutex session_mutex;

// Evitar race conditions quando duas threads tentam escrever o mesmo arquivo no servidor
std::unordered_map<std::string, std::mutex> user_file_mutex;
std::mutex file_mutex_map_guard;

// Função para obter o mutex de um arquivo
std::mutex& get_file_mutex(const std::string& user, const std::string& filename) {
    std::lock_guard<std::mutex> guard(file_mutex_map_guard);
    std::string key = user + "/" + filename;
    return user_file_mutex[key];
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

std::string receive_full_payload(int client_socket) {
    std::string result;
    uint32_t total_received = 0;
    uint32_t total_expected = 0;
    uint16_t current_seqn = 0;

    while (true) {
        Packet pkt;
        if (!recv_exact(client_socket, &pkt, sizeof(Packet))) {
            if (total_received < total_expected) {
                std::cerr << "[ERRO] Falha ao receber pacote completo (antes de atingir o total esperado).\n";
            } else {
                std::cout << "[DEBUG] Conexão encerrada após término da recepção. OK.\n";
            }
            break;
        }

        // Opcional: checar sequência
        if (pkt.seqn != current_seqn) {
            std::cerr << "[ERRO] Sequência incorreta: esperado " << current_seqn << ", recebido " << pkt.seqn << "\n";
            break;
        }

        result.append(pkt._payload, pkt.length);
        total_received += pkt.length;
        total_expected = pkt.total_size;
        current_seqn++;

        if (total_received >= total_expected) break;
    }

    return result;
}

void handle_client(int client_socket) {
    // Recebe o primeiro pacote do cliente (esperado: login)
    Packet login_pkt;
    recv(client_socket, &login_pkt, sizeof(Packet), 0);

    // Extrai o nome do usuário do payload
    std::string username(login_pkt._payload, login_pkt.length);

    // Controle de no máximo 2 dispositivos conectados simultaneamente
    {
        std::lock_guard<std::mutex> lock(session_mutex);

        int& count = user_session_count[username];

        if (count >= 2) {
            std::string msg = "ERRO: Limite de 2 sessões simultâneas excedido.";
            Packet deny = make_packet(CMD, 0, 0, msg.size(), msg);
            send(client_socket, &deny, sizeof(Packet), 0);
            close(client_socket);
            std::cout << msg << "\n";
            return;
        }

        count++;
        std::cout << "[INFO] Sessões ativas para '" << username << "': " << count << "\n";
    }

    std::cout << "[+] Novo cliente conectado: " << username << "\n";
    {
        std::lock_guard<std::mutex> lock(socket_mutex);
        user_sockets[username].push_back(client_socket);
    }

    // Envia confirmação
    std::string msg = "Login bem-sucedido!";
    Packet response = make_packet(CMD, 0, 0, msg.size(), msg);
    send(client_socket, &response, sizeof(Packet), 0);

    // Cria diretório de usuário no servidor se ainda não existir
    std::string user_dir = username + "_sync_dir";
    if (!std::filesystem::exists(user_dir)) {
        std::filesystem::create_directory(user_dir);
        std::cout << "[INFO] Diretório criado para usuário: " << user_dir << "\n";
    } else {
        std::cout << "[INFO] Diretório já existia: " << user_dir << "\n";
    }

    // Loop principal: recebe comandos
    while (true) {
        
        // Recebe sequências de pacotes do cliente
        std::string payload = receive_full_payload(client_socket);
        if (payload.empty()) break;
    
        // Debug
        std::cout << "[DEBUG] Comando recebido de " << username << ":\n" << payload.substr(0, 100) << "\n";
    
        // Trata comando LIST_SERVER
        if (payload == "LIST_SERVER") {
            std::stringstream out;
            list_files(user_dir, out);
            std::string result = out.str();
            Packet reply = make_packet(CMD, 0, 0, result.size(), result);
            send(client_socket, &reply, sizeof(Packet), 0);
        }
    
        // Trata comando DELETE
        else if (payload.rfind("DELETE\n", 0) == 0) {
            std::string filename = payload.substr(7);
            std::string filepath = user_dir + "/" + filename;

            {
                std::lock_guard<std::mutex> lock(get_file_mutex(username, filename));
        
                if (std::filesystem::exists(filepath)) {
                    std::filesystem::remove(filepath);
                    std::cout << "[SYNC] Arquivo deletado no servidor: " << filename << "\n";
                } else {
                    std::cout << "[SYNC] Arquivo para deletar não encontrado: " << filename << "\n";
                }
            }

            // Propagação de DELETE para outros dispositivos
            std::string notification = "DELETE\n" + filename;
            Packet pkt = make_packet(CMD, 0, 0, notification.size(), notification);
            {
                std::lock_guard<std::mutex> lock(socket_mutex);
                for (int sock : user_sockets[username]) {
                    if (sock != client_socket) { // não envia de volta para quem enviou
                        send(sock, &pkt, sizeof(Packet), 0);
                    }
                }
            }
        }
    
        // Trata comando UPLOAD
        else if (payload.rfind("UPLOAD\n", 0) == 0) {
            size_t pos1 = payload.find('\n', 7);
            if (pos1 != std::string::npos) {
                std::string filename = payload.substr(7, pos1 - 7);
                std::string content = payload.substr(pos1 + 1);
                std::string filepath = user_dir + "/" + filename;

                {
                    std::lock_guard<std::mutex> lock(get_file_mutex(username, filename));

                    std::ofstream file(filepath, std::ios::binary);
                    file << content;
                    file.close();
                }

                std::cout << "[SYNC] Arquivo salvo no servidor: " << filename << "\n";

                // Propagação de UPLOAD para outros dispositivos
                std::string notification = "UPLOAD\n" + filename + "\n" + content;
                {
                    std::lock_guard<std::mutex> lock(socket_mutex);
                    for (int sock : user_sockets[username]) {
                        if (sock != client_socket) { // não envia de volta para quem enviou
                            send_large_payload(sock, CMD, notification);
                        }
                    }
                }
            } else {
                std::cerr << "[ERRO] Comando UPLOAD mal formatado.\n";
            }
        }

        // Trata comando DOWNLOAD
        else if (payload.rfind("DOWNLOAD\n", 0) == 0) {
            std::string filename = payload.substr(9);
            std::string filepath = user_dir + "/" + filename;

            {
                std::lock_guard<std::mutex> lock(get_file_mutex(username, filename));
        
                if (std::filesystem::exists(filepath)) {
                    std::ifstream file(filepath, std::ios::binary);
                    std::ostringstream ss;
                    ss << file.rdbuf();
                    std::string content = ss.str();
            
                    std::string full_response = "UPLOAD " + filename + "\n" + content;
                    Packet reply = make_packet(CMD, 0, 0, full_response.size(), full_response);
                    send(client_socket, &reply, sizeof(Packet), 0);
                    std::cout << "[SYNC] Arquivo enviado para download: " << filename << "\n";
                } else {
                    std::string err = "ERRO: Arquivo não encontrado.";
                    Packet reply = make_packet(CMD, 0, 0, err.size(), err);
                    send(client_socket, &reply, sizeof(Packet), 0);
                    std::cerr << "[ERRO] Cliente pediu arquivo inexistente: " << filename << "\n";
                }
            }
        }

        // Trata comando GET_ALL_FILES
        else if (payload == "GET_ALL_FILES") {
            for (const auto& entry : std::filesystem::directory_iterator(user_dir)) {
                if (!entry.is_regular_file()) continue;

                std::string filename = entry.path().filename().string();

                std::ifstream file(entry.path(), std::ios::binary);
                std::ostringstream ss;
                ss << file.rdbuf();
                std::string content = ss.str();

                std::string full = "UPLOAD\n" + filename + "\n" + content;
                send_large_payload(client_socket, CMD, full);
                std::cout << "[SYNC] Enviado arquivo '" << filename << "' ao novo cliente\n";
            }
        }
    
        else {
            std::cerr << "[ERRO] Comando não reconhecido.\n";
        }
    }
    
    // Encerra a conexão com o cliente
    close(client_socket);

    {
        std::lock_guard<std::mutex> lock(session_mutex);
        user_session_count[username]--;
        std::cout << "[INFO] Sessão encerrada. Restam " << user_session_count[username]
                << " conexões para '" << username << "'.\n";
    }

    {
        std::lock_guard<std::mutex> lock(socket_mutex);
        auto& sockets = user_sockets[username];
        sockets.erase(
            std::remove_if(
                sockets.begin(), sockets.end(),
                [client_socket](int s) { return s == client_socket; }
            ),
            sockets.end()
        );
    }
}

void init_server(int port) {
    // Criação do socket TCP
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Define as configurações do endereço (porta e IP)
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Associa o socket à porta especificada
    bind(server_fd, (sockaddr*)&address, sizeof(address));

    // Inicia o modo de escuta por conexões
    listen(server_fd, 10);
    std::cout << "[*] Servidor ouvindo na porta " << port << "...\n";

    // Loop principal que aceita conexões de clientes
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);

        // Aceita nova conexão e cria um novo socket específico para o cliente
        int client_socket = accept(server_fd, (sockaddr*)&client_addr, &addrlen);

        // Cria uma nova thread para tratar o cliente de forma concorrente
        std::thread(handle_client, client_socket).detach();
    }
}

void send_large_payload(int socket, uint16_t type, const std::string& payload) {
    const size_t chunk_size = PAYLOAD_SIZE;
    size_t total_size = payload.size();
    size_t total_sent = 0;
    uint16_t seqn = 0;

    std::cout << "[DEBUG] Enviando payload de tamanho: " << total_size << " bytes\n";

    while (total_sent < total_size) {
        Packet pkt;
        pkt.type = type;
        pkt.seqn = seqn++;
        pkt.total_size = total_size;

        size_t remaining = total_size - total_sent;
        pkt.length = (remaining < chunk_size) ? remaining : chunk_size;

        std::memcpy(pkt._payload, payload.data() + total_sent, pkt.length);

        send(socket, &pkt, sizeof(Packet), 0);

        std::cout << "[DEBUG] Enviado pacote seqn=" << pkt.seqn
                  << " length=" << pkt.length << "\n";

        total_sent += pkt.length;
    }

    std::cout << "[DEBUG] Envio completo de " << seqn << " pacotes\n";
}