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

#include "../common/utils.hpp"
#include "service.hpp"
#include "communication.hpp"

void multicast_primary_info(int multicast_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    // Permitir reuso de endereço
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in multicast_addr{};
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_port = htons(multicast_port);
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP.c_str());

    std::string message = "PRIMARY:" + info.primary_ip;

    int multicast_count = 0;

    while (multicast_count < MULTICAST_ATTEMPTS) {
        sendto(sock, message.c_str(), message.size(), 0, (sockaddr*)&multicast_addr, sizeof(multicast_addr));
        
        std::cout << "[P] Multicast [" << multicast_count+1 << "] enviado\n";
        std::this_thread::sleep_for(std::chrono::seconds(MULTICAST_DELAY));

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
            std::cout << "[B] Servidor Primário falhou. Iniciando eleição...\n";

            // CHAMAR ALGORITMO DE ELEIÇÃO AQUI

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
            info.backups_ip.push_back(std::string(ip_str));

            std::cout << "[INFO] Conexão de backup realizada. Salvando IP " << ip_str << " e socket " << backup_socket << "\n";

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
    std::string replica_msg = "SERVER_INFO|BACKUP_IPS|" + join_backups_ip(info.backups_ip);
    replicate_to_all_backups(replica_msg);
    replica_msg.clear();

    close(replica_fd);
}

void listen_primary_for_replicas(int replication_fd) {
    char buffer[1024];
    std::string leftover;  // Armazena dados incompletos de mensagens anteriores

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
                process_replica(msg);
            }
        }
    }
    close(replication_fd);
}

bool process_replica(std::string msg) {
    std::istringstream ss(msg);
    std::string section, category, username, data;
        
    // Divide por '|'
    std::getline(ss, section, '|');
    std::getline(ss, category, '|');
    std::getline(ss, username, '|');
    std::getline(ss, data);

    bool updated = false;

    if (section == "SERVER_INFO" && category == "CLIENTS") {
        // Inicializa entrada se ainda não existir
        if (info.clients.find(username) == info.clients.end()) {
            info.clients[username] = ClientInfo{};
        }

        // Divide o campo do tipo VALOR
        size_t sep = data.find(':');
        if (sep != std::string::npos) {
                std::string field = data.substr(0, sep);
                std::string value = data.substr(sep + 1);

            if (field == "IP") {
                info.clients[username].ip = value;
                std::cout << "[INFO] Atualizado IP de " << username << " para " << value << "\n";
            
            } else if (field == "USERNAME") {
                info.clients[username].username = value;
                std::cout << "[INFO] Atualizado username de " << username << " para " << value << "\n";
            
            } else if (field == "SESSION_COUNT") {
                try {
                    info.clients[username].session_count = std::stoi(value);
                    std::cout << "[INFO] Atualizado session_count de " << username << " para " << value << "\n";

                } catch (...) {
                    std::cerr << "[ERRO] Erro ao converter session_count: " << value << "\n";
                    return false;
                }

            } else if (field == "SOCKETS") {
                try {
                    int socket_val = std::stoi(value);
                    auto& user_sockets = info.clients[username].sockets;
                    if (std::find(user_sockets.begin(), user_sockets.end(), socket_val) == user_sockets.end()) {
                        user_sockets.push_back(socket_val);
                        std::cout << "[INFO] Adicionado socket de " << username << ": " << socket_val << "\n";

                    } else {
                        std::cout << "[DEBUG] Socket " << socket_val << " de " << username << " já presente\n";
                    }

                } catch (...) {
                    std::cerr << "[ERRO] Erro ao converter socket: " << value << "\n";
                    return false;
                }
                
            } else if (field == "REMOVE_SOCKET") {
                 try {
                    int socket_to_remove = std::stoi(value);
                    auto& user_sockets = info.clients[username].sockets;
                    user_sockets.erase(
                        std::remove(user_sockets.begin(), user_sockets.end(), socket_to_remove),
                        user_sockets.end()
                    );
                    std::cout << "[INFO] Removido socket de " << username << ": " << value << "\n";

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

    } else if (section == "SERVER_INFO" && category == "BACKUP_IPS") {
        info.backups_ip.clear(); // reseta para evitar duplicatas

        std::stringstream ss(data);
        std::string ip;
        while (std::getline(ss, ip, ',')) {
            info.backups_ip.push_back(ip);
        }
        std::cout << "[INFO] IPs de servidores backup atualizados \n";

        updated = true;

    } else {
        std::cerr << "[ERRO] Campo desconhecido: " << section << "\n";
        return false;
    }

    return updated;
}

std::string get_local_ip() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    // Permitir reuso de endereço
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

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

void print_server_info() {
    std::cout << "\n========== ESTADO DO SERVIDOR ==========\n";

    std::cout << "Tipo: ";
    switch (info.type) {
        case ServerType::PRIMARY: std::cout << "PRIMÁRIO\n"; break;
        case ServerType::BACKUP:  std::cout << "BACKUP\n"; break;
    }

    std::cout << "IP do servidor primário: " << info.primary_ip << "\n";

    // Mostrar IPs dos backups (válido apenas no primário)
    if (info.type == ServerType::PRIMARY) {
        std::cout << "Backups conectados (" << info.backups_ip.size() << "):\n";
        for (const auto& ip : info.backups_ip) {
            std::cout << "  - " << ip << "\n";
        }
    }

    std::cout << "\nClientes conectados (" << info.clients.size() << "):\n";

    if (info.clients.empty()) {
        std::cout << "  Nenhum cliente conectado.\n";
    } else {
        for (const auto& [username, client] : info.clients) {
            std::cout << "  Usuário: " << username << "\n";
            std::cout << "    IP: " << client.ip << "\n";
            std::cout << "    Sessões ativas: " << client.session_count << "\n";
            std::cout << "    Sockets: ";
            if (client.sockets.empty()) {
                std::cout << "Nenhum\n";
            } else {
                for (int s : client.sockets) {
                    std::cout << s << " ";
                }
                std::cout << "\n";
            }
        }
    }
    std::cout << "========================================\n\n";
}