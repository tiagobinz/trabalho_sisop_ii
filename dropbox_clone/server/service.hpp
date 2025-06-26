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
#include <functional>

/*
 * server/service.hpp
 * Módulo de serviços no lado servidor para a replicação passiva
 */

#define MULTICAST_GROUP         std::string("239.0.0.1")
#define BACKUP_TIMEOUT          10
#define HEARTBEAT_DELAY         2
#define HEARTBEAT_TIMEOUT       6

#define CLIENT_PORT             12345
#define HEARTBEAT_PORT          12347
#define REPLICA_PORT            12348
#define ELECTION_PORT           12349
#define CLIENT_NOTIFY_PORT      12350

typedef struct ClientInfo {
    std::string username;
    int session_count = 0;
    std::vector<int> sockets;
    std::vector<std::pair<int,std::string>> connections;

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
    std::unordered_map<int, std::string> backups;

    // dados backups
    int election_id;
    bool election_started = false;
    bool backup_replicas_received = false;

} ServerInfo;

extern ServerInfo info;

void send_heartbeat_to_backups(int multicast_port);
void listen_heartbeat_from_server(int port);

void listen_backup_to_connect(int replication_fd);
void listen_primary_for_replicas(int replication_fd);
bool handle_replica(std::string msg);

void send_election_id_to_primary(int backup_fd);
int receive_election_id_from_backup(int backup_socket, const std::string& ip_str);
void election_listener();
bool handle_election(int election_socket, std::function<void()> on_election_end);

void init_primary_services();
void init_backup_services();
void notify_clients_of_new_primary(const ServerInfo& info);

void check_user_directory(std::string username);

void print_server_info();

#endif