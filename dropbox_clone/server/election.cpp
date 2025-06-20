#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <mutex>
#include <atomic>
#include "election.hpp"

// Constantes para o algoritmo
const int ELECTION_TIMEOUT = 3; // segundos
const int COORDINATOR_TIMEOUT = 5; // segundos
const int CONNECTION_TIMEOUT = 2; // segundos
const std::string ELECTION_MSG = "ELECTION";
const std::string OK_MSG = "OK";
const std::string COORDINATOR_MSG = "COORDINATOR";

// Estrutura para representar um backup
struct BackupNode {
    std::string ip;
    int port;
    int priority; // Usado como ID para o algoritmo Bully
    
    BackupNode(std::string ip, int port, int priority) 
        : ip(ip), port(port), priority(priority) {}
};

// Variáveis globais para controle da eleição
std::atomic<bool> election_in_progress(false);
std::atomic<bool> received_ok(false);
std::string current_coordinator = "";
std::mutex coordinator_mutex;

// Função para configurar timeout em socket
void set_socket_timeout(int sock, int seconds) {
    struct timeval timeout;
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

// Função para enviar mensagem TCP para um nó específico
bool send_tcp_message(const std::string& ip, int port, const std::string& message) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    
    // Configurar timeout
    set_socket_timeout(sock, CONNECTION_TIMEOUT);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    
    // Tentar conectar
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }
    
    // Enviar mensagem
    ssize_t sent = send(sock, message.c_str(), message.length(), 0);
    bool success = (sent > 0);
    
    close(sock);
    return success;
}

// Função para enviar mensagem TCP e receber resposta
std::string send_tcp_message_with_response(const std::string& ip, int port, const std::string& message) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return "";
    }
    
    // Configurar timeout
    set_socket_timeout(sock, CONNECTION_TIMEOUT);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    
    // Tentar conectar
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return "";
    }
    
    // Enviar mensagem
    ssize_t sent = send(sock, message.c_str(), message.length(), 0);
    if (sent <= 0) {
        close(sock);
        return "";
    }
    
    // Receber resposta
    char buffer[1024];
    ssize_t received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    
    close(sock);
    
    if (received > 0) {
        buffer[received] = '\0';
        return std::string(buffer);
    }
    
    return "";
}

// Função para lidar com conexões TCP recebidas
void handle_tcp_connection(int client_sock, const std::string& client_ip) {
    char buffer[1024];
    ssize_t received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    
    if (received > 0) {
        buffer[received] = '\0';
        std::string message(buffer);
        
        std::cout << "[ELECTION] Mensagem recebida de " << client_ip << ": " << message << "\n";
        
        if (message == ELECTION_MSG) {
            // Responder com OK e iniciar própria eleição
            std::string response = OK_MSG;
            send(client_sock, response.c_str(), response.length(), 0);
            
            std::cout << "[ELECTION] Respondendo OK e iniciando própria eleição\n";
            received_ok = true;
            
        } else if (message == COORDINATOR_MSG) {
            // Aceitar novo coordenador
            std::lock_guard<std::mutex> lock(coordinator_mutex);
            current_coordinator = client_ip;
            std::cout << "[ELECTION] Novo coordenador aceito: " << client_ip << "\n";
            
            std::string response = "ACK";
            send(client_sock, response.c_str(), response.length(), 0);
        }
    }
    
    close(client_sock);
}

void tcp_election_server(int port) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        std::cerr << "[ELECTION] Erro ao criar socket servidor\n";
        return;
    }
    
    int reuse = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[ELECTION] Erro no bind do servidor\n";
        close(server_sock);
        return;
    }
    
    if (listen(server_sock, 10) < 0) {
        std::cerr << "[ELECTION] Erro no listen\n";
        close(server_sock);
        return;
    }
    
    std::cout << "[ELECTION] Servidor TCP escutando na porta " << port << "\n";
    
    // Configurar timeout para accept
    set_socket_timeout(server_sock, 1);
    
    while (election_in_progress) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        
        int client_sock = accept(server_sock, (sockaddr*)&client_addr, &addr_len);
        
        if (client_sock >= 0) {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            
            // Criar thread para lidar com a conexão
            std::thread client_thread(handle_tcp_connection, client_sock, std::string(client_ip));
            client_thread.detach();
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    close(server_sock);
}

std::string Election(std::unordered_map<std::string, int> backups) {
    std::cout << "[ELECTION] Iniciando eleição\n";
    
    election_in_progress = true;
    received_ok = false;
    current_coordinator = "";
    
    // Converter o mapa para vetor de BackupNode e ordenar por prioridade
    std::vector<BackupNode> nodes;
    
    // Assumindo que a prioridade é baseada no último octeto do IP
    for (const auto& backup : backups) {
        std::string ip = backup.first;
        int port = backup.second;
        
        // Extrair prioridade do IP (último octeto)
        size_t last_dot = ip.find_last_of('.');
        int priority = std::stoi(ip.substr(last_dot + 1));
        
        nodes.emplace_back(ip, port, priority);
    }
    
    // Ordenar por prioridade (maior prioridade primeiro)
    std::sort(nodes.begin(), nodes.end(), 
              [](const BackupNode& a, const BackupNode& b) {
                  return a.priority > b.priority;
              });
    
    // Determinar nossa própria prioridade
    int my_priority = nodes.empty() ? 1000 : nodes.back().priority + 1000;
    
    std::cout << "[ELECTION] Minha prioridade: " << my_priority << "\n";
    
    // Iniciar servidor TCP para escutar mensagens de eleição
    const int election_port = 9999;
    std::thread server_thread(tcp_election_server, election_port);
    
    // Aguardar um pouco para o servidor inicializar
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // FASE 1: Enviar mensagens ELECTION para nós com prioridade maior
    std::vector<BackupNode> higher_priority_nodes;
    for (const auto& node : nodes) {
        if (node.priority > my_priority) {
            higher_priority_nodes.push_back(node);
        }
    }
    
    if (!higher_priority_nodes.empty()) {
        std::cout << "[ELECTION] Enviando ELECTION para nós com prioridade maior...\n";
        
        // Enviar mensagens ELECTION e aguardar respostas OK
        for (const auto& node : higher_priority_nodes) {
            std::cout << "[ELECTION] Enviando ELECTION para " << node.ip << ":" << node.port << "\n";
            
            std::string response = send_tcp_message_with_response(node.ip, node.port, ELECTION_MSG);
            
            if (response == OK_MSG) {
                std::cout << "[ELECTION] Recebido OK de " << node.ip << "\n";
                received_ok = true;
            }
        }
    }
    
    std::string new_coordinator = "";
    
    if (!received_ok) {
        // FASE 2: Nenhum nó com prioridade maior respondeu, eu sou o coordenador
        std::cout << "[ELECTION] Nenhuma resposta OK recebida. Tornando-me coordenador!\n";
        
        // Enviar mensagens COORDINATOR para todos os outros nós
        for (const auto& node : nodes) {
            if (node.priority < my_priority) {
                std::cout << "[ELECTION] Enviando COORDINATOR para " << node.ip << ":" << node.port << "\n";
                
                bool sent = send_tcp_message(node.ip, node.port, COORDINATOR_MSG);
                if (!sent) {
                    std::cout << "[ELECTION] Falha ao enviar COORDINATOR para " << node.ip << "\n";
                }
            }
        }
        
        new_coordinator = "127.0.0.1"; // Assumindo que eu sou o localhost
        std::cout << "[ELECTION] EU sou o novo coordenador!\n";
        
    } else {
        // FASE 3: Aguardar mensagem COORDINATOR do novo líder
        std::cout << "[ELECTION] Aguardando mensagem COORDINATOR do novo líder...\n";
        
        auto start_time = std::chrono::steady_clock::now();
        
        while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - start_time).count() < COORDINATOR_TIMEOUT) {
            
            {
                std::lock_guard<std::mutex> lock(coordinator_mutex);
                if (!current_coordinator.empty()) {
                    new_coordinator = current_coordinator;
                    break;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Se não recebeu COORDINATOR, iniciar nova eleição
        if (new_coordinator.empty()) {
            std::cout << "[ELECTION] Timeout esperando COORDINATOR. Reiniciando eleição...\n";
            election_in_progress = false;
            server_thread.join();
            return Election(backups); // Recursão para reiniciar eleição
        }
    }
    
    // Finalizar servidor
    election_in_progress = false;
    server_thread.join();
    
    std::cout << "[ELECTION] Eleição concluída. Novo líder: " << new_coordinator << "\n";
    return new_coordinator;
}