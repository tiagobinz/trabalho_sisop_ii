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

    // Servidor PRIMÁRIO
    if (argc == 4 && std::string(argv[1]) == "-p") {
        std::string backup1_ip = argv[2];
        std::string backup2_ip = argv[3];

        std::cout << "[INFO] Iniciando servidor PRIMÁRIO na porta " << CLIENT_PORT << "\n";

        info.type = ServerType::PRIMARY;
        info.primary_ip = info.ip = get_local_ip();
        info.backups.emplace(backup1_ip, 0);
        info.backups.emplace(backup2_ip, 0);

        print_server_info();
        init_primary_services();

    // Servidor BACKUP
    } else if (argc == 3 && std::string(argv[1]) == "-b") {
        std::string primary_ip = argv[2];

        std::cout << "[INFO] Iniciando servidor BACKUP na porta " << REPLICA_PORT << "\n";

        info.type = ServerType::BACKUP;
        info.primary_ip = primary_ip;
        info.ip = get_local_ip();
        info.election_id = generate_random_election_id();

        print_server_info();
        init_backup_services();

    } else {
        std::cerr << "Uso:\n";
        std::cerr << "  Para servidor primário: ./myServer -p <backup1_ip> <backup2_ip>\n";
        std::cerr << "  Para servidor backup:   ./myServer -b <primary_ip>\n";
        return 1;
    }

    return 0;
}