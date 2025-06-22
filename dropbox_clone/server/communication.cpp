#include "communication.hpp"
#include "service.hpp"
#include "../common/packet.hpp"
#include "../common/utils.hpp"

#include <sys/socket.h>
#include <arpa/inet.h>
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

// Mutex dos sockets ativos por usuário
std::mutex socket_mutex;

// Mutex das sessões dos clients
std::mutex session_mutex;

// Mutex dos sockets dos backups
std::vector<int> backup_sockets;
std::mutex backup_mutex;

void register_backup_socket(int backup_socket) {
    std::lock_guard<std::mutex> lock(backup_mutex);
    backup_sockets.push_back(backup_socket);
}

// Evitar race conditions quando duas threads tentam escrever o mesmo arquivo no servidor
std::unordered_map<std::string, std::mutex> user_file_mutex;
std::mutex file_mutex_map_guard;

// Função para obter o mutex de um arquivo
std::mutex& get_file_mutex(const std::string& user, const std::string& filename) {
    std::lock_guard<std::mutex> guard(file_mutex_map_guard);
    std::string key = user + "/" + filename;
    return user_file_mutex[key];
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

    // String para as replicas
    std::string replica_msg;

    // Recebe o primeiro pacote do cliente (esperado: login)
    Packet login_pkt;
    recv(client_socket, &login_pkt, sizeof(Packet), 0);

    // Extrai o nome do usuário do payload
    std::string username(login_pkt._payload, login_pkt.length);

    info.clients[username].ip = get_client_ip(client_socket);
    replica_msg = "SERVER_INFO|CLIENTS|" + username + "|IP:" + info.clients[username].ip;
    replicate_to_all_backups(replica_msg);
    replica_msg.clear();

    if (info.clients[username].username.empty()) {
        info.clients[username].username = username;
        replica_msg = "SERVER_INFO|CLIENTS|" + username + "|USERNAME:" + info.clients[username].username;
        replicate_to_all_backups(replica_msg);
        replica_msg.clear();
    }

    // Controle de no máximo 2 dispositivos conectados simultaneamente
    {
        std::lock_guard<std::mutex> lock(session_mutex);

        int& count = info.clients[username].session_count;

        if (count >= 2) {
            std::string msg = "[DEBUG] Limite de 2 sessões simultâneas excedido.";
            Packet deny = make_packet(CMD, 0, 0, msg.size(), msg);
            send(client_socket, &deny, sizeof(Packet), 0);
            close(client_socket);
            std::cout << msg << "\n";
            return;
        }

        count++;

        replica_msg = "SERVER_INFO|CLIENTS|" + username + "|SESSION_COUNT:" + std::to_string(count);
        replicate_to_all_backups(replica_msg);
        replica_msg.clear();

        std::cout << "[INFO] Sessões ativas para '" << username << "': " << count << "\n";
    }

    std::cout << "[+] Novo cliente conectado: " << username << "\n";
    {
        std::lock_guard<std::mutex> lock(socket_mutex);
        info.clients[username].sockets.push_back(client_socket);

        replica_msg = "SERVER_INFO|CLIENTS|" + username + "|SOCKETS:" + std::to_string(client_socket);
        replicate_to_all_backups(replica_msg);
        replica_msg.clear();
    }

    // Envia confirmação
    std::string msg = "Login bem-sucedido!";
    Packet response = make_packet(CMD, 0, 0, msg.size(), msg);
    send(client_socket, &response, sizeof(Packet), 0);

    // Cria diretório de usuário no servidor se ainda não existir
    std::string user_dir = username + "_sync_dir";
    check_user_directory(user_dir);

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
                for (int sock : info.clients[username].sockets) {
                    if (sock != client_socket) { // não envia de volta para quem enviou
                        send(sock, &pkt, sizeof(Packet), 0);
                    }
                }
            }
        }
    
        // Trata comando UPLOAD
        else if (payload.rfind("UPLOAD\n", 0) == 0) {
            size_t pos1 = payload.find('\n', 7);
            size_t pos2 = payload.find('\n', pos1 + 1);
            if (pos1 != std::string::npos) {
                std::string filename = payload.substr(7, pos1 - 7);
                std::string ts_str = payload.substr(pos1 + 1, pos2 - pos1 - 1);
                std::string content = payload.substr(pos2 + 1);

                std::cout << "[DEBUG] Filename: " << filename << "\n";
                std::cout << "[DEBUG] Timestamp: " << ts_str << "\n";
                std::cout << "[DEBUG] Content length: " << content.size() << "\n";

                std::cout << "[DEBUG] content[0] = " << content[0] << "\n";

                std::string filepath = user_dir + "/" + filename;
                auto ts_remote = std::stoll(ts_str);

                bool salvar = true;

                // Verificar timestamp
                if (std::filesystem::exists(filepath)) {
                    auto local_ts = std::chrono::duration_cast<std::chrono::seconds>(
                        std::filesystem::last_write_time(filepath).time_since_epoch()).count();
                    salvar = ts_remote >= local_ts;

                    std::cout << "[DEBUG] local_ts = " << local_ts << "\n";
                }

                if (salvar)
                {
                    std::cout << "[SYNC] Começando a salvar o arquivo no servidor: " << filename << "\n";

                    // Atualizar o arquivo no servidor
                    {
                        std::lock_guard<std::mutex> lock(get_file_mutex(username, filename));

                        std::ofstream file(filepath, std::ios::binary);
                        file << content;
                        file.close();

                        // Atualiza o timestamp
                        auto new_time = std::filesystem::file_time_type(std::chrono::seconds(ts_remote));
                        std::filesystem::last_write_time(filepath, new_time);

                        // Converte para time_t
                        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                        new_time - std::filesystem::file_time_type::clock::now()
                                        + std::chrono::system_clock::now());

                        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);

                        std::cout << "last_write_time: " << std::put_time(std::localtime(&cftime), "%F %T") << "\n";
                    }

                    std::cout << "[SYNC] Arquivo salvo no servidor: " << filename << "\n";

                    // Propagação de UPLOAD para outros dispositivos
                    std::string notification = "UPLOAD\n" + filename + "\n" + ts_str + "\n" + content;
                    {
                        std::lock_guard<std::mutex> lock(socket_mutex);
                        for (int sock : info.clients[username].sockets) {
                            if (sock != client_socket) { // não envia de volta para quem enviou
                                send_large_payload(sock, CMD, notification);
                            }
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

                auto ftime = std::filesystem::last_write_time(entry.path());
                auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                    ftime.time_since_epoch()).count();

                std::string full_command = "UPLOAD\n" + filename + "\n" + std::to_string(ts) + "\n" + content;
                send_large_payload(client_socket, CMD, full_command);
                std::cout << "[SYNC] Enviado arquivo '" << filename << "' ao novo cliente\n";
            }
        } else if (payload == "INFO") {
            print_server_info();
        }
    
        else {
            std::cerr << "[ERRO] Comando não reconhecido.\n";
        }
    }
    
    // Encerra a conexão com o cliente
    close(client_socket);

    {
        std::lock_guard<std::mutex> lock(session_mutex);
        info.clients[username].session_count--;

        replica_msg = "SERVER_INFO|CLIENTS|" + username + "|SESSION_COUNT:" + std::to_string(info.clients[username].session_count);
        replicate_to_all_backups(replica_msg);
        replica_msg.clear();

        std::cout << "[INFO] Sessão encerrada. Restam " << info.clients[username].session_count
                << " conexões para '" << username << "'.\n";
    }

    {
        std::lock_guard<std::mutex> lock(socket_mutex);
        auto& sockets = info.clients[username].sockets;
        sockets.erase(
            std::remove_if(
                sockets.begin(), sockets.end(),
                [client_socket](int s) { return s == client_socket; }
            ),
            sockets.end()
        );

        replica_msg = "SERVER_INFO|CLIENTS|" + username + "|REMOVE_SOCKET:" + std::to_string(client_socket);
        replicate_to_all_backups(replica_msg);
        replica_msg.clear();

        replica_msg = "SERVER_INFO|CLIENTS|" + username + "|IP:";
        replicate_to_all_backups(replica_msg);
        replica_msg.clear();
    }
}

int init_server(int port, ServerType t) {

    if (t == ServerType::PRIMARY) {
        // Criação do socket TCP (aceitar clientes)
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
        //std::cout << "[*] Servidor ouvindo na porta " << port << "...\n";
        return server_fd;

    } else if (t == ServerType::BACKUP) {

        // Criação do socket TCP (escutar server primário)
        int backup_fd = socket(AF_INET, SOCK_STREAM, 0);

        // Define as configurações com o servidor primário
        sockaddr_in primary_addr{};
        primary_addr.sin_family = AF_INET;
        primary_addr.sin_port = htons(port);

        // Converte o IP string para binário
        if (inet_pton(AF_INET, info.primary_ip.c_str(), &primary_addr.sin_addr) <= 0) {
            std::cerr << "[ERRO] inet_pton falhou. IP: " << info.primary_ip << " inválido? \n";
            close(backup_fd);
            return -1;
        }

        // Conecta ao primário
        if (connect(backup_fd, (sockaddr*)&primary_addr, sizeof(primary_addr)) < 0) {
            std::cerr << "[ERRO] Falha ao conectar-se ao servidor.\n";
            close(backup_fd);
            return -1;
        }
        std::cout << "[B] Conectado ao primário em " << info.primary_ip << ":" << port << "\n";
        return backup_fd;

    } else {
        std::cout << "[ERRO] Erro ao iniciar servidor" << "\n";
        return -1;
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

void replicate_to_all_backups(const std::string& replica_msg) {
    std::lock_guard<std::mutex> lock(backup_mutex);
    std::string final_msg = replica_msg + "\n";

    for (int sock : backup_sockets) {
        ssize_t sent = send(sock, final_msg.c_str(), final_msg.size(), 0);
        if (sent < 0) {
            perror("[ERRO] Erro ao enviar réplica para backup");
        } else {
            std::cout << "[P] Réplica enviada para backup no socket " << sock << "\n";
        }
    }
}