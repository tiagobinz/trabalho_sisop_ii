#include "communication.hpp"
#include "../common/packet.hpp"
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <cstring>

void handle_client(int client_socket) {
    // Recebe o primeiro pacote do cliente (esperado: login)
    Packet login_pkt;
    recv(client_socket, &login_pkt, sizeof(Packet), 0);

    // Extrai o nome do usuário do payload
    std::string username(login_pkt._payload, login_pkt.length);
    std::cout << "[+] Novo cliente conectado: " << username << "\n";

    // Envia confirmação
    std::string msg = "Login bem-sucedido!";
    Packet response = make_packet(CMD, 0, 0, msg.size(), msg);
    send(client_socket, &response, sizeof(Packet), 0);

    // Encerra a conexão com o cliente
    close(client_socket);
}

void init_server(int port) {
    // Criação do socket TCP
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Define as configurações do endereço (porta e IP)
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Associa o socket à porta especificada
    bind(server_fd, (sockaddr*)&address, sizeof(address));

    // Inicia o modo de escuta por conexões
    listen(server_fd, 10);
    std::cout << "[*] Servidor ouvindo na porta " << port << "...\n";

    // Loop principal que aceita conexões de clientes
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);

        // Aceita nova conexão e cria um novo socket específico para o cliente
        int client_socket = accept(server_fd, (sockaddr*)&client_addr, &addrlen);

        // Cria uma nova thread para tratar o cliente de forma concorrente
        std::thread(handle_client, client_socket).detach();
    }
}
