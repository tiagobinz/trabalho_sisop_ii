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
#define BACKUP_TIMEOUT          10
#define HEARTBEAT_DELAY         2
#define HEARTBEAT_TIMEOUT       6

#define CLIENT_PORT             12345
#define MULTICAST_PORT          12346
#define HEARTBEAT_PORT          12347
#define REPLICA_PORT            12348
#define ELECTION_PORT           12349

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
    std::unordered_map<int, std::string> backups;

    // dados backups
    int election_id;
    bool election_started = false;
    bool backup_replicas_received = false;

} ServerInfo;

extern ServerInfo info;


void multicast_primary_info(int multicast_port);
std::string listen_for_primary_multicast(int multicast_port);

void send_heartbeat_to_backups(int multicast_port);
void listen_heartbeat_from_server(int port);

void listen_backup_to_connect(int replication_fd);
void listen_primary_for_replicas(int replication_fd);
bool handle_replica(std::string msg);

void send_election_id_to_primary(int backup_fd);
int receive_election_id_from_backup(int backup_socket, const std::string& ip_str);

void check_user_directory(std::string username);

void election_listener();
bool handle_election(int election_socket);

void print_server_info();

#endif