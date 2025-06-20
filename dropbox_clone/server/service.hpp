/*
 *                   UNIVERSIDADE FEDERAL DO RIO GRANDE DO SUL           
 *                           INSTITUTO DE INFORMÁTICA                    
 *                      DEPARTAMENTO DE INFORMÁTICA APLICADA
 * 
 * INF01151 – SISTEMAS OPERACIONAIS II N
 * SEMESTRE 2025/1
 * TRABALHO PRÁTICO
 * 
 * INTEGRANTES DO GRUPO:
 * Gabriel Alves Bohrer
 * Igor Dalpiaz Bauer Chaves
 * Tiago Ehlers Binz
 * Victor de Souza Arnt
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <string>
#include <vector>
#include <unordered_map>

/*
 * server/service.hpp
 * Módulo de serviços no lado servidor para a replicação passiva
 */

#define MULTICAST_GROUP         std::string("239.0.0.1")
#define MULTICAST_DELAY         3
#define MULTICAST_ATTEMPTS      5
#define BACKUP_TIMEOUT          20
#define HEARTBEAT_DELAY         2
#define HEARTBEAT_TIMEOUT       6

#define CLIENT_PORT             12345
#define MULTICAST_PORT          12346
#define HEARTBEAT_PORT          12347
#define REPLICA_PORT            12348
extern const int REPLICATION_PORT;
extern const int SYNC_PORT;
typedef struct ClientInfo {
    std::string username;
    std::string ip = "";
    int session_count = 0;
    std::vector<int> sockets;

} ClientInfo;

enum class ServerType {
    PRIMARY,
    BACKUP
 };
typedef struct ServerInfo {
    ServerType type;
    std::string primary_ip;
    std::string ip;
    std::unordered_map<std::string, ClientInfo> clients;
    std::unordered_map<std::string, int> backups;

} ServerInfo;

extern ServerInfo info;

// Declarações das funções
void multicast_primary_info(int multicast_port);
std::string listen_for_primary_multicast(int multicast_port);
void send_heartbeat_to_backups(int multicast_port);
void listen_heartbeat_from_server(int port);
void listen_backup_to_connect(int replica_fd);
void listen_primary_for_replicas(int replication_fd);
bool process_replica(std::string msg);

// Novas funções
void request_state_from_backup(const std::string& backup_ip);
void process_state_sync_response(const std::string& response);
bool is_server_claiming_primary(const std::string& server_ip);
std::string resolve_primary_conflict(const std::vector<std::string>& candidates);
void stop_primary_services();
void promote_to_primary();
void reconnect_to_new_primary();
int connect_to_primary_for_replication();
void start_primary_services();
int setup_backup_listener();
void synchronize_state_with_peers();
void notify_clients_of_primary_change();
bool handle_concurrent_elections();
void demote_to_backup(const std::string& new_primary_ip);

// Funções auxiliares
std::string get_local_ip();
std::string get_client_ip(int client_socket);
size_t generate_random_election_id();
void send_election_id_to_primary(int backup_fd);
int receive_election_id_from_backup(int backup_socket, const std::string& ip_str);
void print_server_info();
void multicast_primary_info(int multicast_port);
std::string listen_for_primary_multicast(int multicast_port);

void send_heartbeat_to_backups(int multicast_port);
void listen_heartbeat_from_server(int port);

void listen_backup_to_connect(int replication_fd);
void listen_primary_for_replicas(int replication_fd);
bool process_replica(std::string msg);

std::string get_local_ip();
std::string get_client_ip(int client_socket);

size_t generate_random_election_id();
void send_election_id_to_primary(int backup_fd);
int receive_election_id_from_backup(int backup_socket, const std::string& ip_str);

void print_server_info();

#endif