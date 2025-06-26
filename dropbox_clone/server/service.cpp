#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "../common/utils.hpp"
#include "service.hpp"
#include "communication.hpp"
#include "election.hpp"

void send_heartbeat_to_backups(int multicast_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    // Permitir reuso de endereço
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in multicast_addr{};
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_port = htons(multicast_port);
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP.c_str());

    std::string heartbeat_msg = "HEARTBEAT";

    while (true) {
        sendto(sock, heartbeat_msg.c_str(), heartbeat_msg.size(), 0,
               (sockaddr*)&multicast_addr, sizeof(multicast_addr));
        std::cout << "[P] Heartbeat enviado aos backups\n";
        std::this_thread::sleep_for(std::chrono::seconds(HEARTBEAT_DELAY));
    }

    close(sock);
}

void listen_heartbeat_from_server(int port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }

    // Para socket ser nao-bloqueante
    fcntl(sock, F_SETFL, O_NONBLOCK);

    // Permitir reuso de endereço
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

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
        if (elapsed > HEARTBEAT_TIMEOUT) {

            if (!info.backups.empty()) {
                int min_id = std::numeric_limits<int>::max();
                for (const auto& [id, _] : info.backups) {
                    min_id = std::min(min_id, id);
                }
            
                if (info.election_id <= min_id) {
                    std::cout << "[INFO] Primário falhou. Iniciando eleição...\n";
                    start_election(info.backups);
                }
                
            } else {
                std::cout << "[INFO] Primário falhou. Iniciando eleição...\n";
                start_election(info.backups);
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    close(sock);
}

void listen_backup_to_connect(int replica_fd) {
    using clock = std::chrono::steady_clock;
    auto last_connection_time = clock::now();

    while (true) {
        // Definimos o socket como não-bloqueante para poder checar timeout
        fcntl(replica_fd, F_SETFL, O_NONBLOCK);

        sockaddr_in backup_addr{};
        socklen_t addrlen = sizeof(backup_addr);
        int backup_socket = accept(replica_fd, (sockaddr*)&backup_addr, &addrlen);

        if (backup_socket >= 0) {
            last_connection_time = clock::now();

            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &backup_addr.sin_addr, ip_str, sizeof(ip_str));

            register_backup_socket(backup_socket);
            int election_id = receive_election_id_from_backup(backup_socket, ip_str);
            info.backups[election_id] = std::string(ip_str);

            std::cout << "[P] Conexão com backup realizada:\n";
            std::cout << "[INFO] IP: " << info.backups[election_id] << " | election_id: " << election_id << "\n";
        } else {
            auto now = clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_connection_time).count();

            if (elapsed > BACKUP_TIMEOUT) {
                std::cout << "[INFO] Nenhuma nova conexão de backup há "
                          << BACKUP_TIMEOUT << " segundos\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }

    // Depois que todos os backups se conectaram ao primário, replica a lista dos IPs dos backups para posterior eleição
    std::string replica_msg = "SERVER_INFO|BACKUPS|" + join_backups(info.backups);
    replicate_to_all_backups(replica_msg);

    close(replica_fd);
}

void listen_primary_for_replicas(int replication_fd) {
    char buffer[1024];
    std::string leftover;  // Armazena dados incompletos de mensagens anteriores

    //std::thread([replication_fd]() {
    //    while (true) {
    //        std::cout << "Esperando Replica de arquivos...\n";
    //        std::string payload = receive_payload(replication_fd);
    //        handle_replica(payload);
    //    }
    //}).detach();

    while (true) {
        ssize_t len = recv(replication_fd, buffer, sizeof(buffer) - 1, 0);
        if (len <= 0) {
            std::cout << "[B] Conexão com primário encerrada.\n";
            break;
        }

        buffer[len] = '\0';
        leftover += buffer;

        size_t pos;
        while ((pos = leftover.find('\n')) != std::string::npos) {
            std::string msg = leftover.substr(0, pos);
            leftover.erase(0, pos + 1);  // Remove a mensagem processada
            if (!msg.empty()) {
                std::cout << "Réplica recebida: " << msg << "\n";
                handle_replica(msg);
            }
        }
    }
    close(replication_fd);
}

bool handle_replica(std::string msg) {
    std::istringstream ss(msg);
    
    std::string section, category, username, data, data2;
        
    // Divide por '|'
    std::getline(ss, section, '|');
    std::getline(ss, category, '|');
    std::getline(ss, username, '|');
    std::getline(ss, data, '|');
    std::getline(ss, data2);

    bool updated = false;

    if(section == "UPLOAD"){

        std::string user = category;
        std::string filename = username;
        std::string ts_str = data;
        std::string content = data2;

        std::string filepath = user + "_sync_dir/" + filename;
        auto ts_remote = std::stoll(ts_str);

        bool salvar = true;

        if (std::filesystem::exists(filepath)) {
            auto local_ts = std::chrono::duration_cast<std::chrono::seconds>(
                std::filesystem::last_write_time(filepath).time_since_epoch()
            ).count();
            salvar = ts_remote >= local_ts;
        }

        if (salvar) {
            std::lock_guard<std::mutex> lock(get_file_mutex(user, filename));
            std::ofstream file(filepath, std::ios::binary);
            file << content;
            file.close();

            auto new_time = std::filesystem::file_time_type(std::chrono::seconds(ts_remote));
            std::filesystem::last_write_time(filepath, new_time);

            std::cout << "[REPLICA] Arquivo atualizado: " << filename << " de " << user << "\n";
            updated = true;
        }

    } else if (section == "SERVER_INFO" && category == "CLIENTS") {
        // Inicializa entrada se ainda não existir
        if (info.clients.find(username) == info.clients.end()) {
            info.clients[username] = ClientInfo{};
        }

        // Divide o campo do tipo VALOR
        size_t sep = data.find(':');
        if (sep != std::string::npos) {
                std::string field = data.substr(0, sep);
                std::string value = data.substr(sep + 1);
            
            if (field == "USERNAME") {
                info.clients[username].username = value;
                std::string user_dir = value + "_sync_dir";
                check_user_directory(user_dir);
            
            } else if (field == "SESSION_COUNT") {
                try {
                    info.clients[username].session_count = std::stoi(value);
                    std::cout << "[INFO] Atualizado session_count de " << username << " para " << value << "\n";

                } catch (...) {
                    std::cerr << "[ERRO] Erro ao converter session_count: " << value << "\n";
                    return false;
                }

            } else if (field == "CONNECT") {
                try {
                    // value deve estar no formato "<socket>:<ip>"
                    size_t delim_pos = value.find(':');
                    if (delim_pos == std::string::npos) {
                        std::cerr << "[ERRO] Formato inválido em CONNECT: " << value << "\n";
                        return false;
                    }
                
                    int socket_val = std::stoi(value.substr(0, delim_pos));
                    std::string ip = value.substr(delim_pos + 1);

                    auto& connections = info.clients[username].connections;

                    // Verifica se o socket já está registrado
                    auto it = std::find_if(connections.begin(), connections.end(),
                        [socket_val](const std::pair<int, std::string>& conn) {
                            return conn.first == socket_val;
                        }
                    );
                
                    if (it == connections.end()) {
                        connections.emplace_back(socket_val, ip);
                        std::cout << "[INFO] Adicionada conexão de " << username << ": socket=" << socket_val << ", ip=" << ip << "\n";
                    } else {
                        std::cout << "[DEBUG] Conexão já presente para " << username << ": socket=" << socket_val << "\n";
                    }
            
                } catch (...) {
                    std::cerr << "[ERRO] Erro ao processar CONNECT: " << value << "\n";
                    return false;
                }

            } else if (field == "REMOVE_CONNECT") {
                try {
                    int socket_to_remove = std::stoi(value);
                    auto& connections = info.clients[username].connections;
                
                    size_t before = connections.size();
                    connections.erase(
                        std::remove_if(
                            connections.begin(), connections.end(),
                            [socket_to_remove](const std::pair<int, std::string>& conn) {
                                return conn.first == socket_to_remove;
                            }
                        ),
                        connections.end()
                    );
                    size_t after = connections.size();
                
                    if (after < before) {
                        std::cout << "[INFO] Removida conexão (socket + IP) de " << username << ": socket " << socket_to_remove << "\n";
                    } else {
                        std::cout << "[DEBUG] Conexão com socket " << socket_to_remove << " não encontrada para " << username << "\n";
                    }
                
                } catch (...) {
                    std::cerr << "[ERRO] Erro ao converter socket: " << value << "\n";
                    return false;
                }
            } else {
                std::cerr << "[ERRO] Campo de CLIENT não encontrado";
                return false;
            }
            updated = true;
        }
                
    } else if (section == "SERVER_INFO" && category == "BACKUPS") {
        //info.backups.clear(); // reseta para evitar duplicatas

        std::stringstream ss(username);
        std::string pair;

        int count = 0;
        while (std::getline(ss, pair, ',')) {
            if (pair.empty()) continue;

            size_t sep = pair.find(':');
            if (sep == std::string::npos) {
                std::cerr << "[DEBUG] Par inválido (faltando ':'): " << pair << "\n";
                continue;
            }

            std::string id_str = pair.substr(0, sep);
            std::string ip = pair.substr(sep + 1);

            try {
                int id = std::stoi(id_str);

                if(!(id == info.election_id)) {
                    info.backups[id] = ip;
                    std::cout << "[INFO] Backup ID: " << id << " | IP: " << ip << " adicionado.\n";
                    count++;
                } else {
                    std::cout << "[DEBUG] ID " << id << " não adicionado\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "[ERRO] ID inválido no par: " << pair << " (" << e.what() << ")\n";
            }
        }

        if (count > 0) {
            std::cout << "[INFO] IPs de servidores backup atualizados. Total: " << count << "\n";
            updated = true;
            info.backup_replicas_received = true;
        } else {
            std::cerr << "[ERRO] Nenhum backup válido foi adicionado a partir de: " << username << "\n";
        }
    }
    return updated;
}

void send_election_id_to_primary(int backup_fd) {
    std::string msg = "ELECTION_ID:" + std::to_string(info.election_id);
    send(backup_fd, msg.c_str(), msg.size(), 0);
    std::cout << "[B] Enviado election_id: " << info.election_id << " ao primário\n";
}

int receive_election_id_from_backup(int backup_socket, const std::string& ip_str) {
    char buffer[256];
    ssize_t len = recv(backup_socket, buffer, sizeof(buffer) - 1, 0);

    int election_id = -1;

    if (len > 0) {
        buffer[len] = '\0';
        std::string msg(buffer);

        if (msg.rfind("ELECTION_ID:", 0) == 0) {
            std::string id_str = msg.substr(std::string("ELECTION_ID:").length());
            election_id = std::stoi(id_str);
            if(election_id == 0) 
                std::cerr << "[ERRO] ID inválido recebido do backup " << ip_str << ": " << id_str << "\n";
                
        } else {
            std::cerr << "[ERRO] Mensagem inesperada recebida do backup " << ip_str
                      << ": " << msg << "\n";
        }
    } else {
        std::cerr << "[ERRO] Falha ao receber election_id do backup " << ip_str << "\n";
    }
    return election_id;
}

void election_listener() {

    // Fica aguardando até receber do primário os dados dos backups
    while (!info.backup_replicas_received) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    int backup_fd = init_server(get_port_by_id(info.election_id), ServerType::PRIMARY);
    std::cout << "[E] Servidor ID " << info.election_id << " escutando na porta " << get_port_by_id(info.election_id) << "\n";

    while(true) {
        // Cria socket para o backup
        sockaddr_in backup_addr{};
        socklen_t addrlen = sizeof(backup_addr);

        int election_socket = accept(backup_fd, (sockaddr*)&backup_addr, &addrlen);
        
        // Cria uma nova thread para tratar as mensagens de eleição
        std::thread ([election_socket] {
            handle_election(election_socket, [] {
                // Callback true que vem quando recebe COORDINATOR
                init_backup_services();
            });
        }).detach();
    }
}

bool handle_election(int election_socket, std::function<void()> on_election_end) {

    // Receber a mensagem que vier de election_socket
    char buffer[256];
    ssize_t len = recv(election_socket, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        close(election_socket);
        std::cerr << "[ERRO] Socket \n";
        return false;
    }

    buffer[len] = '\0';
    std::string msg(buffer);

    // Recebeu ELECTION de outro servidor
    if (msg.rfind("ELECTION|", 0) == 0) {
        int received_id = std::stoi(msg.substr(9));

        std::cout << "[E] Recebida ELECTION de ID: " << received_id << "\n";

        std::string answer_msg = "ANSWER|" + std::to_string(info.election_id);
        
        send(election_socket, answer_msg.c_str(), answer_msg.size(), 0);

        std::cout << "[E] Enviada ANSWER para ID: " << received_id << "\n";

        // Iniciar minha própria eleição
        if (!election_state.election_in_progress) {
            std::thread([&] {
                start_election(info.backups);
            }).detach();
        }

    // Novo primário foi eleito
    } else if (msg.rfind("COORDINATOR|", 0) == 0) {
        
        { std::lock_guard<std::mutex> lock(election_state.election_mutex);
            info.election_started = false;
            election_state.election_in_progress = false;
            info.backup_replicas_received = false;
        }

        int new_primary_id = std::stoi(msg.substr(12));
        info.primary_ip = info.backups[new_primary_id];
        close(election_socket);

        for (auto& [id, ip] : info.backups) {
            if(id == info.election_id) {
                info.backups[id].erase();
            }
        }
        std::cout << "[E] *** NOVO PRIMÁRIO ELEITO: ID " << new_primary_id << " | IP: " << info.backups[new_primary_id] << " ***\n\n";
        
        on_election_end();
        return true;

    // Recebeu resposta de um servidor com ID maior
    } else if (msg.rfind("ANSWER|", 0) == 0) {
        std::lock_guard<std::mutex> lock(election_state.election_mutex);

        int id = std::stoi(msg.substr(7));
        std::cout << "[E] Recebido ANSWER de ID " << id << "\n";
        
        election_state.answer_received = true;
    }

    return false;
}

void init_primary_services() {
    int server_fd = init_server(CLIENT_PORT, info.type);

    // Cria thread para enviar heartbeat constantemente para todos os backups
    std::thread(send_heartbeat_to_backups, HEARTBEAT_PORT).detach();

    // Cria thread para guardar metados dos backups
    int replica_fd = init_server(REPLICA_PORT, info.type);
    std::thread(listen_backup_to_connect, replica_fd).detach();

    // Loop principal que aceita conexões de clientes
    while (true) {
        // Cria socket para o cliente
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int client_socket = accept(server_fd, (sockaddr*)&client_addr, &addrlen);

        // Cria uma nova thread para tratar o cliente de forma concorrente
        std::thread(handle_client, client_socket).detach();
    }
}

void init_backup_services() {
    // Cria uma nova thread para executar o "protocolo heartbeat"
    std::thread(listen_heartbeat_from_server, HEARTBEAT_PORT).detach();

    // Cria uma nova thread para escutar por mensagens de eleição
    std::thread(election_listener).detach();

    int backup_fd = init_server(REPLICA_PORT, info.type);
    send_election_id_to_primary(backup_fd);

    // Loop principal que recebe replicas do primário
    listen_primary_for_replicas(backup_fd);
}

void notify_clients_of_new_primary(const ServerInfo& info) {
    std::string msg = "NEW_PRIMARY|" + info.primary_ip;
    std::cout << "[E] Notificando clientes sobre novo primário: " << msg << "\n";

    for (const auto& [username, client] : info.clients) {
        for (const auto& [_, client_ip] : client.connections) {

            sockaddr_in client_addr{};
            client_addr.sin_family = AF_INET;
            client_addr.sin_port = htons(CLIENT_NOTIFY_PORT);
            inet_pton(AF_INET, client_ip.c_str(), &client_addr.sin_addr);

            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                perror("[ERRO] socket");
                continue;
            }

            if (connect(sock, (sockaddr*)&client_addr, sizeof(client_addr)) == 0) {
                send(sock, msg.c_str(), msg.size(), 0);
                std::cout << "[INFO] Notificado " << username << " em " << client_ip << "\n";
            } else {
                std::cerr << "[ERRO] Falha ao conectar no cliente " << username << " (" << client_ip << ")\n";
            }
            close(sock);
        }
    }
}

void check_user_directory(std::string user_dir) {

    if (!std::filesystem::exists(user_dir)) {
        std::filesystem::create_directory(user_dir);
        std::cout << "[INFO] Diretório criado para usuário: " << user_dir << "\n";
    } else {
        std::cout << "[INFO] Diretório já existia: " << user_dir << "\n";
    }
}

void print_server_info() {
    std::cout << "\n========== ESTADO DO SERVIDOR ==========\n";

    std::cout << "Tipo: ";
    switch (info.type) {
        case ServerType::PRIMARY: std::cout << "PRIMÁRIO\n"; break;
        case ServerType::BACKUP:  std::cout << "BACKUP\n"; break;
    }

    std::cout << "IP do servidor primário: " << info.primary_ip << "\n";

    if(info.backups.empty()) {
        std::cout << "Nenhum backup conectado\n";
    } else {
        std::cout << "Backups conectados (" << info.backups.size() << "):\n";
        for (const auto& [id, ip] : info.backups) {
            std::cout << " ID: " << id << " | IP: " << ip << "\n";
        }
    }

    if (info.clients.empty()) {
        std::cout << "Nenhum cliente conectado\n";
    } else {
        std::cout << "\nClientes conectados (" << info.clients.size() << "):\n";

        for (const auto& [username, client] : info.clients) {
            std::cout << "  Usuário: " << username << "\n";
            std::cout << "    Sessões ativas: " << client.session_count << "\n";
            
            if (client.connections.empty()) {
                std::cout << "    Nenhuma conexão ativa\n";
            } else {
                std::cout << "    Conexões (socket, ip):\n";
                for (const auto& [sock, ip] : client.connections) {
                    std::cout << "      Socket: " << sock << " | IP: " << ip << "\n";
                }
            }
        }
    }

    std::cout << "========================================\n\n";
}