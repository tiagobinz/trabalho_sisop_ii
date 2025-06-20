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
#include <random>

#include "../common/utils.hpp"
#include "service.hpp"
#include "communication.hpp"
#include "election.hpp"
const int REPLICATION_PORT = 12346;
const int SYNC_PORT = 12347;
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
            std::string new_primary_ip = Election(info.backups);
            
            // Verificar se este servidor foi eleito como novo primário
            std::string my_ip = get_local_ip();
            if (new_primary_ip == my_ip) {
                std::cout << "[ELEIÇÃO] Este servidor foi eleito como novo primário!\n";
                promote_to_primary();
            } else {
                std::cout << "[ELEIÇÃO] Novo primário eleito: " << new_primary_ip << "\n";
                // Atualizar informação do novo primário
                info.primary_ip = new_primary_ip;
                // Reconectar ao novo primário
                reconnect_to_new_primary();
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
            info.backups[ip_str] = election_id;

            std::cout << "[P] Conexão com backup realizada:\n";
            std::cout << "[INFO] IP: " << ip_str << " | election_id: " << info.backups[ip_str] << "\n";
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

    while (true) {
        ssize_t len = recv(replication_fd, buffer, sizeof(buffer) - 1, 0);
        if (len <= 0) {
            std::cout << "[B] Conexão com primário encerrada.\n";
            
            // CHAMAR ALGORITMO DE ELEIÇÃO AQUI
            std::string new_primary_ip = Election(info.backups);
            
            // Verificar se este servidor foi eleito como novo primário
            std::string my_ip = get_local_ip();
            if (new_primary_ip == my_ip) {
                std::cout << "[ELEIÇÃO] Este servidor foi eleito como novo primário!\n";
                promote_to_primary();
            } else {
                std::cout << "[ELEIÇÃO] Novo primário eleito: " << new_primary_ip << "\n";
                // Atualizar informação do novo primário
                info.primary_ip = new_primary_ip;
                // Reconectar ao novo primário
                reconnect_to_new_primary();
            }
            
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

// Nova função para promover um backup a primário
void promote_to_primary() {
    std::cout << "[TRANSIÇÃO] Promovendo este servidor de BACKUP para PRIMÁRIO...\n";
    
    // Sincronizar estado com outros backups se necessário
    synchronize_state_with_peers();
    
    // Alterar o tipo do servidor
    info.type = ServerType::PRIMARY;
    
    // Atualizar o IP do primário para o IP local
    info.primary_ip = get_local_ip();
    
    std::cout << "[TRANSIÇÃO] Servidor agora é PRIMÁRIO com IP: " << info.primary_ip << "\n";
    
    // Remover este servidor da lista de backups
    info.backups.erase(info.primary_ip);
    
    // Aguardar um momento para estabilizar
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Iniciar multicast para anunciar novo primário
    std::thread multicast_thread([]{
        multicast_primary_info(MULTICAST_PORT);
    });
    multicast_thread.detach();
    
    // Iniciar thread de heartbeat para os backups
    std::thread heartbeat_thread([]{
        send_heartbeat_to_backups(MULTICAST_PORT);
    });
    heartbeat_thread.detach();
    
    // Iniciar servidor para aceitar conexões de backups
    start_primary_services();
    
    // Notificar clientes sobre mudança de primário (se necessário)
    notify_clients_of_primary_change();
    
    std::cout << "[TRANSIÇÃO] Transição para PRIMÁRIO concluída!\n";
}

// Nova função para reconectar a um novo primário
void reconnect_to_new_primary() {
    std::cout << "[RECONEXÃO] Tentando reconectar ao novo primário: " << info.primary_ip << "\n";
    
    // Aguardar um pouco para o novo primário se estabelecer
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Tentar estabelecer conexão com o novo primário
    int attempts = 0;
    const int max_attempts = 5;
    
    while (attempts < max_attempts) {
        try {
            // Conectar ao novo primário para replicação
            int replication_socket = connect_to_primary_for_replication();
            if (replication_socket > 0) {
                std::cout << "[RECONEXÃO] Conectado ao novo primário com sucesso!\n";
                
                // Iniciar thread para escutar replicações do novo primário
                std::thread replication_thread([replication_socket]{
                    listen_primary_for_replicas(replication_socket);
                });
                replication_thread.detach();
                
                // Iniciar thread para escutar heartbeats do novo primário
                std::thread heartbeat_thread([]{
                    listen_heartbeat_from_server(MULTICAST_PORT);
                });
                heartbeat_thread.detach();
                
                break;
            }
        } catch (const std::exception& e) {
            std::cerr << "[RECONEXÃO] Erro na tentativa " << (attempts + 1) << ": " << e.what() << "\n";
        }
        
        attempts++;
        if (attempts < max_attempts) {
            std::cout << "[RECONEXÃO] Tentativa " << attempts << " falhou. Tentando novamente em 3 segundos...\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
    
    if (attempts >= max_attempts) {
        std::cerr << "[RECONEXÃO] Falha ao reconectar após " << max_attempts << " tentativas.\n";
        // Aqui você pode decidir iniciar uma nova eleição ou tentar outras estratégias
    }
}

// Nova função para conectar ao primário para replicação
int connect_to_primary_for_replication() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        throw std::runtime_error("Erro ao criar socket");
    }

    sockaddr_in primary_addr{};
    primary_addr.sin_family = AF_INET;
    primary_addr.sin_port = htons(REPLICATION_PORT);
    inet_pton(AF_INET, info.primary_ip.c_str(), &primary_addr.sin_addr);

    if (connect(sock, (sockaddr*)&primary_addr, sizeof(primary_addr)) < 0) {
        close(sock);
        throw std::runtime_error("Erro ao conectar ao primário");
    }

    return sock;
}

// Nova função para iniciar serviços quando promovido a primário
void start_primary_services() {
    std::cout << "[SERVIÇOS] Iniciando serviços do primário...\n";
    
    // Aqui você deve iniciar todos os serviços necessários para um primário:
    // - Servidor para aceitar clientes
    // - Servidor para aceitar backups
    // - Qualquer outro serviço específico do seu sistema
    
    // Exemplo de inicialização de servidor para backups
    std::thread backup_listener_thread([]{
        int replica_fd = setup_backup_listener();
        if (replica_fd > 0) {
            listen_backup_to_connect(replica_fd);
        }
    });
    backup_listener_thread.detach();
}

// Função auxiliar para configurar listener de backups
int setup_backup_listener() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "[ERRO] Falha ao criar socket para backups\n";
        return -1;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(REPLICATION_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[ERRO] Falha no bind para backups\n";
        close(sock);
        return -1;
    }

    if (listen(sock, 10) < 0) {
        std::cerr << "[ERRO] Falha no listen para backups\n";
        close(sock);
        return -1;
    }

    std::cout << "[SERVIÇOS] Servidor de backups iniciado na porta " << REPLICATION_PORT << "\n";
    return sock;
}

// Nova função para sincronizar estado com outros servidores
void synchronize_state_with_peers() {
    std::cout << "[SYNC] Sincronizando estado com outros servidores...\n";
    
    // Implementar lógica para garantir que este servidor tem o estado mais atualizado
    // Pode envolver consultar outros backups ou usar timestamps das últimas operações
    
    // Exemplo de implementação:
    for (const auto& [backup_ip, election_id] : info.backups) {
        if (backup_ip != get_local_ip()) {
            try {
                request_state_from_backup(backup_ip);
            } catch (const std::exception& e) {
                std::cerr << "[SYNC] Erro ao sincronizar com " << backup_ip << ": " << e.what() << "\n";
            }
        }
    }
    
    std::cout << "[SYNC] Sincronização de estado concluída\n";
}

// Função para solicitar estado de um backup específico
void request_state_from_backup(const std::string& backup_ip) {
    // Implementar conexão temporária com backup para solicitar estado
    int sync_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sync_sock < 0) return;
    
    sockaddr_in backup_addr{};
    backup_addr.sin_family = AF_INET;
    backup_addr.sin_port = htons(SYNC_PORT); // Porta específica para sincronização
    inet_pton(AF_INET, backup_ip.c_str(), &backup_addr.sin_addr);
    
    if (connect(sync_sock, (sockaddr*)&backup_addr, sizeof(backup_addr)) == 0) {
        std::string sync_request = "STATE_REQUEST";
        send(sync_sock, sync_request.c_str(), sync_request.size(), 0);
        
        // Receber e processar resposta de estado
        char buffer[4096];
        ssize_t len = recv(sync_sock, buffer, sizeof(buffer) - 1, 0);
        if (len > 0) {
            buffer[len] = '\0';
            process_state_sync_response(std::string(buffer));
        }
    }
    
    close(sync_sock);
}

// Função para processar resposta de sincronização
void process_state_sync_response(const std::string& response) {
    // Implementar lógica para processar estado recebido
    // e atualizar info.clients se necessário
    std::cout << "[SYNC] Processando resposta de sincronização\n";
}

// Função para notificar clientes sobre mudança de primário
void notify_clients_of_primary_change() {
    std::cout << "[NOTIFY] Notificando clientes sobre mudança de primário...\n";
    
    std::string notification = "PRIMARY_CHANGED:" + info.primary_ip;
    
    // Enviar notificação para todos os clientes conectados
    for (const auto& [username, client] : info.clients) {
        for (int socket : client.sockets) {
            send(socket, notification.c_str(), notification.size(), 0);
        }
    }
    
    std::cout << "[NOTIFY] Notificação enviada para " << info.clients.size() << " clientes\n";
}

// Função para tratar eleições concorrentes (split-brain prevention)
bool handle_concurrent_elections() {
    std::cout << "[ELEIÇÃO] Verificando eleições concorrentes...\n";
    
    // Implementar lógica para detectar se há múltiplos primários
    // Pode usar heartbeats ou consultas diretas
    
    std::vector<std::string> active_primaries;
    
    // Verificar se há outros servidores se anunciando como primário
    for (const auto& [backup_ip, election_id] : info.backups) {
        if (is_server_claiming_primary(backup_ip)) {
            active_primaries.push_back(backup_ip);
        }
    }
    
    if (active_primaries.size() > 1) {
        std::cout << "[ELEIÇÃO] Detectadas eleições concorrentes! Resolvendo...\n";
        
        // Resolver usando IDs de eleição ou timestamps
        std::string actual_primary = resolve_primary_conflict(active_primaries);
        
        if (actual_primary != get_local_ip()) {
            std::cout << "[ELEIÇÃO] Cedendo primário para " << actual_primary << "\n";
            // Voltar para modo backup
            demote_to_backup(actual_primary);
            return false;
        }
    }
    
    return true;
}

// Função para verificar se um servidor está se anunciando como primário
bool is_server_claiming_primary(const std::string& server_ip) {
    // Implementar verificação (pode ser via multicast ou conexão direta)
    return false; // Placeholder
}

// Função para resolver conflito entre múltiplos primários
std::string resolve_primary_conflict(const std::vector<std::string>& candidates) {
    // Implementar lógica de desempate (maior ID de eleição, menor IP, etc.)
    return candidates.empty() ? "" : candidates[0]; // Placeholder
}

// Função para rebaixar servidor de primário para backup
void demote_to_backup(const std::string& new_primary_ip) {
    std::cout << "[TRANSIÇÃO] Rebaixando de PRIMÁRIO para BACKUP...\n";
    
    // Parar serviços de primário
    stop_primary_services();
    
    // Alterar tipo para backup
    info.type = ServerType::BACKUP;
    info.primary_ip = new_primary_ip;
    
    // Reconectar ao novo primário
    reconnect_to_new_primary();
    
    std::cout << "[TRANSIÇÃO] Transição para BACKUP concluída\n";
}

// Função para parar serviços de primário
void stop_primary_services() {
    std::cout << "[SERVIÇOS] Parando serviços de primário...\n";
    // Implementar lógica para parar threads e fechar sockets
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
                if (!value.empty()) {
                    std::cout << "[INFO] Atualizado IP de " << username << " para " << value << "\n";
                } else {
                    std::cout << "[INFO] Removido IP de " << username << "\n";
                }
            
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

    } else if (section == "SERVER_INFO" && category == "BACKUPS") {
        info.backups.clear(); // reseta para evitar duplicatas

        std::stringstream ss(data);
        std::string pair;

        while (std::getline(ss, pair, ',')) {
            size_t sep = pair.find(':');

            if (sep != std::string::npos) {
                std::string ip = pair.substr(0, sep);
                int id = std::stoi(pair.substr(sep + 1));
                info.backups[ip] = id;
            }
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

size_t generate_random_election_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(1000, 999999);
    return dist(gen);
}

void send_election_id_to_primary(int backup_fd) {
    int id = generate_random_election_id();
    std::string msg = "ELECTION_ID:" + std::to_string(id);
    send(backup_fd, msg.c_str(), msg.size(), 0);
    std::cout << "[B] Enviado election_id: " << id << " ao primário\n";
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
        std::cout << "Backups conectados (" << info.backups.size() << "):\n";
        for (const auto& [ip, id] : info.backups) {
            std::cout << " IP: " << ip << "\n";
            std::cout << " ID: " << id << "\n";
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