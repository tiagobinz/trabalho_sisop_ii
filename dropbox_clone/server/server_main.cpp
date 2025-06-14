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
    int port = 12345; // porta padrão
    int port2 = 12346; // porta heartbeat

    if (argc != 2) {
        std::cerr << "Uso: ./myServer <type>\n";
        return 1;
    }

    // Extração dos argumentos
    std::string server_type = argv[1];  // -p (primary), -b (backup)
    
    ServerType t;

    if (server_type == "-p") {
        std::cout << "Iniciando servidor PRIMÁRIO na porta " << port << "\n";
        t = ServerType::PRIMARY;
        int server_fd = init_server(port, t);

        // Cria thread para enviar heartbeat constantemente para todos os backups
        std::thread(send_heartbeat_to_backups, port2).detach();

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
        std::cout << "Iniciando servidor BACKUP na porta " << port << "\n";
        t = ServerType::BACKUP;
        int backup_fd = init_server(port, t);

        // Cria uma nova thread para executar o "protocolo heartbeat"
        std::thread(listen_for_heartbeat, port2).detach();

        // Loop principal para receber novas replicas do servidor primário
        while (true) {

        }
    
    } else {
        std::cerr << "Erro: tipo de servidor inválido. Use -p para primário ou -b para backup.\n";
        return 1;
    }

    return 0;
}
