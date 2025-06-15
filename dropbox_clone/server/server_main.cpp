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

int main(int argc, char* argv[]) {

    if (argc != 2) {
        std::cerr << "Uso: ./myServer <type>\n";
        return 1;
    }
    // Extração dos argumentos
    std::string server_type = argv[1];  // -p (primary), -b (backup)
    
    ServerType t;

    if (server_type == "-p") {
        std::cout << "Iniciando servidor PRIMÁRIO na porta " << CLIENT_PORT << "\n";
        t = ServerType::PRIMARY;

        int server_fd = init_server(CLIENT_PORT, t);
        // Cria uma nova thread para realizar alguns Multicasts UDP e avisar aos backups o endereço primário
        std::thread(multicast_primary_info, MULTICAST_PORT).detach();

        // Cria thread para enviar heartbeat constantemente para todos os backups
        std::thread(send_heartbeat_to_backups, HEARTBEAT_PORT).detach();

        int replica_fd = init_server(REPLICA_PORT, t);
        // Cria thread para receber conexões dos backups e salvar sockets
        std::thread(listen_sockets_from_backups, replica_fd).detach();

        // Loop principal que aceita conexões de clientes
        while (true) {
            // Cria socket para o cliente
            sockaddr_in client_addr{};
            socklen_t addrlen = sizeof(client_addr);
            int client_socket = accept(server_fd, (sockaddr*)&client_addr, &addrlen);

            // Cria uma nova thread para tratar o cliente de forma concorrente
            std::thread(handle_client, client_socket).detach();
        }

    } else if (server_type == "-b") {
        std::cout << "Iniciando servidor BACKUP na porta " << REPLICA_PORT << "\n";
        t = ServerType::BACKUP;
        int backup_fd = init_server(REPLICA_PORT, t);

        // Cria uma nova thread para executar o "protocolo heartbeat"
        std::thread(listen_heartbeat_from_server, HEARTBEAT_PORT).detach();

        // Loop principal que aceita conexões do server para receber replicas
        while(true) {
            std::this_thread::sleep_for(std::chrono::seconds(10)); // evita busy-wait
        }
        
    
    } else {
        std::cerr << "Erro: tipo de servidor inválido. Use -p para primário ou -b para backup.\n";
        return 1;
    }

    return 0;
}
