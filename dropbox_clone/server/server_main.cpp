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

ServerInfo info;

int main(int argc, char* argv[]) {

    if (argc != 2) {
        std::cerr << "Uso: ./myServer <type>\n";
        return 1;
    }
    // Extração dos argumentos
    std::string server_type = argv[1];  // -p (primary), -b (backup)
    
    // SERVER PRIMÁRIO
    if (server_type == "-p") {
        std::cout << "Iniciando servidor PRIMÁRIO na porta " << CLIENT_PORT << "\n";
        
        info.type = ServerType::PRIMARY;
        info.primary_ip = get_local_ip();

        int server_fd = init_server(CLIENT_PORT, info.type);

        // Cria uma nova thread para realizar alguns Multicasts UDP e avisar aos backups o endereço primário
        std::thread(multicast_primary_info, MULTICAST_PORT).detach();

        // Cria thread para enviar heartbeat constantemente para todos os backups
        std::thread(send_heartbeat_to_backups, HEARTBEAT_PORT).detach();

        // Cria thread para guardar sockets e IPs dos backups
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

    // SERVER BACKUP
    } else if (server_type == "-b") {
        std::cout << "Iniciando servidor BACKUP na porta " << REPLICA_PORT << "\n";
        
        info.type = ServerType::BACKUP;
        info.primary_ip = listen_for_primary_multicast(MULTICAST_PORT);
        info.backups_ip.push_back(get_local_ip());

        int backup_fd = init_server(REPLICA_PORT, info.type);

        // Cria uma nova thread para executar o "protocolo heartbeat"
        std::thread(listen_heartbeat_from_server, HEARTBEAT_PORT).detach();

        // Loop principal que aceita conexões do server para receber replicas
        while(true) {

            Packet pkt;
            if (recv(backup_fd, &pkt, sizeof(Packet), 0) > 0) {
                std::string payload(pkt._payload, pkt.length);
                std::cout << "[INFO] Replica recebida: " << payload << "\n";

                if (payload.rfind("DELETE\n", 0) == 0) {


                } else if (payload.rfind("UPLOAD\n", 0) == 0) {


                } else if (payload.rfind("GET_ALL_FILES\n", 0) == 0) {


                }
            }
        }
        
    } else {
        std::cerr << "Erro: tipo de servidor inválido. Use -p para primário ou -b para backup.\n";
        return 1;
    }

    return 0;
}
