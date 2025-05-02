#include "communication.hpp"
#include "../common/packet.hpp"
#include "../common/utils.hpp"
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <cstring>
#include <filesystem>
#include <fstream>

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

    // Cria diretório de usuário no servidor se ainda não existir
    std::string user_dir = username + "_sync_dir";
    std::filesystem::create_directory(user_dir);

    // Loop principal: recebe comandos
    while (true) {
        Packet pkt;
        ssize_t bytes = recv(client_socket, &pkt, sizeof(Packet), 0);
        if (bytes <= 0) {
            std::cout << "[-] Cliente '" << username << "' desconectado.\n";
            break;
        }
    
        std::string payload(pkt._payload, pkt.length);
    
        // Debug
        std::cout << "[DEBUG] Comando recebido de " << username << ":\n" << payload.substr(0, 100) << "\n";
    
        // Trata comando LIST_SERVER
        if (payload == "LIST_SERVER") {
            std::stringstream out;
            list_files(user_dir, out);
            std::string result = out.str();
            Packet reply = make_packet(CMD, 0, 0, result.size(), result);
            send(client_socket, &reply, sizeof(Packet), 0);
        }
    
        // Trata comando DELETE
        else if (payload.rfind("DELETE\n", 0) == 0) {
            std::string filename = payload.substr(7);
            std::string filepath = user_dir + "/" + filename;
    
            if (std::filesystem::exists(filepath)) {
                std::filesystem::remove(filepath);
                std::cout << "[SYNC] Arquivo deletado no servidor: " << filename << "\n";
            } else {
                std::cout << "[SYNC] Arquivo para deletar não encontrado: " << filename << "\n";
            }
        }
    
        // Trata comando UPLOAD
        else if (payload.rfind("UPLOAD\n", 0) == 0) {
            size_t pos1 = payload.find('\n', 7);
            if (pos1 != std::string::npos) {
                std::string filename = payload.substr(7, pos1 - 7);
                std::string content = payload.substr(pos1 + 1);
                std::string filepath = user_dir + "/" + filename;

                std::ofstream file(filepath, std::ios::binary);
                file << content;
                file.close();

                std::cout << "[SYNC] Arquivo salvo no servidor: " << filename << "\n";
            } else {
                std::cerr << "[ERRO] Comando UPLOAD mal formatado.\n";
            }
        }

        // Trata comando DOWNLOAD
        else if (payload.rfind("DOWNLOAD\n", 0) == 0) {
            std::string filename = payload.substr(9);
            std::string filepath = user_dir + "/" + filename;
        
            if (std::filesystem::exists(filepath)) {
                std::ifstream file(filepath, std::ios::binary);
                std::ostringstream ss;
                ss << file.rdbuf();
                std::string content = ss.str();
        
                std::string full_response = "UPLOAD " + filename + "\n" + content;
                Packet reply = make_packet(CMD, 0, 0, full_response.size(), full_response);
                send(client_socket, &reply, sizeof(Packet), 0);
                std::cout << "[SYNC] Arquivo enviado para download: " << filename << "\n";
            } else {
                std::string err = "ERRO: Arquivo não encontrado.";
                Packet reply = make_packet(CMD, 0, 0, err.size(), err);
                send(client_socket, &reply, sizeof(Packet), 0);
                std::cerr << "[ERRO] Cliente pediu arquivo inexistente: " << filename << "\n";
            }
        }
    
        else {
            std::cerr << "[ERRO] Comando não reconhecido.\n";
        }
    }
    

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
