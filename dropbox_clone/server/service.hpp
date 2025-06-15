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
#define MAX_SERVER_MULTICAST    10
#define HEARTBEAT_DELAY         10

#define CLIENT_PORT             12345
#define MULTICAST_PORT          12346
#define HEARTBEAT_PORT          12347
#define REPLICA_PORT            12348

typedef struct ClientInfo {
    std::string username = "";
    std::string ip = "";
    int session_count = 0;

} ClientInfo;


void multicast_primary_info(int multicast_port);
std::string listen_for_primary_multicast(int multicast_port);

void send_heartbeat_to_backups(int multicast_port);
void listen_heartbeat_from_server(int port);

void listen_sockets_from_backups(int replication_fd);

std::string get_local_ip();
std::string get_client_ip(int client_socket);


#endif