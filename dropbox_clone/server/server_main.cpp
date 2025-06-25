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

/*
 * server_main.cpp
 * Ponto de entrada da aplicação Servidor
 */

#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <thread>

#include "communication.hpp"
#include "service.hpp"
#include "../common/packet.hpp"
#include "../common/utils.hpp"

ServerInfo info;

int main(int argc, char* argv[]) {

    if (argc < 2) {
        std::cerr << "Uso:\n"
                  << "  Primário: ./myServer -p\n"
                  << "  Backup  : ./myServer -b <primary_ip>\n";
        return 1;
    }

    std::string server_type = argv[1];

    // SERVER PRIMÁRIO
    if (server_type == "-p") {
        if (argc != 2) {
            std::cerr << "[ERRO] Uso correto para servidor primário: ./myServer -p <backup1_ip> <backup2_ip>\n";
            return 1;
        }

        std::cout << "[INFO] Iniciando servidor PRIMÁRIO na porta " << CLIENT_PORT << "\n";

        info.type = ServerType::PRIMARY;
        info.ip = get_local_ip();
        info.primary_ip = info.ip;

        init_primary_services();

    // SERVER BACKUP
    } else if (server_type == "-b") {
        if (argc != 3) {
            std::cerr << "[ERRO] Uso correto para servidor backup: ./myServer -b <primary_ip>\n";
            return 1;
        }

        std::string primary_ip = argv[2];

        std::cout << "[INFO] Iniciando servidor BACKUP na porta " << REPLICA_PORT << "\n";

        info.type = ServerType::BACKUP;
        info.primary_ip = primary_ip;
        info.ip = get_local_ip();
        info.election_id = generate_random_election_id();

        init_backup_services();

    } else {
        std::cerr << "[ERRO] Tipo de servidor inválido. Use -p para primário ou -b para backup.\n";
        return 1;
    }

    return 0;
}