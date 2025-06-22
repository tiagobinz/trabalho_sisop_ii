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

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "communication.hpp"
#include "sync_manager.hpp"
#include "../common/packet.hpp"
#include "../common/utils.hpp"

#define CLIENT_PORT             12345
#define CLIENT_NOTIFY_PORT      12350

/*
 * client_main.cpp
 * Ponto de entrada da aplicação Cliente
 */

void command_loop(const std::string& username);
bool reconnect_to_primary(const std::string& new_ip, const std::string& username);
void server_listener();

std::atomic<bool> sync_started = false;
std::atomic<bool> should_reconnect = false;
std::string current_server_ip;
std::string current_username;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Uso: ./myClient <username> <server_ip>\n";
        return 1;
    }

    // Extrai os argumentos
    current_username = argv[1];
    current_server_ip = argv[2];

    // Inicia a thread que escuta mudanças de primário
    std::thread(server_listener).detach();

    // Primeira tentativa de conexão
    if (!connect_to_server_TCP(current_server_ip, CLIENT_PORT)) {
        std::cerr << "[ERRO] Conexão inicial com o servidor falhou.\n";
        return 1;
    }

    // Envia pacote de login
    Packet login_pkt = make_packet(CMD, 0, 0, current_username.size(), current_username);
    send_packet(login_pkt);
    std::cout << "[INFO] Login enviado: " << current_username << "\n";

    // Aguarda resposta do servidor
    Packet response = receive_packet();
    std::string response_msg(response._payload, response.length);

    if (response_msg.rfind("ERRO", 0) == 0) {
        std::cerr << "[SERVIDOR] " << response_msg << "\n";
        return 1;
    } else {
        std::cout << "[SERVIDOR] " << response_msg << "\n";
    }

    sync_started = get_sync_dir(current_username);
    std::cout << "[INFO] Cliente pronto.\n";

    // Inicia o loop de comandos interativo
    command_loop(current_username);

    return 0;
}

bool reconnect_to_primary(const std::string& new_ip, const std::string& username) {
    //close(client_socket);  // Fecha conexão atual, se existir
    std::cout << "[INFO] Tentando reconectar ao novo primário: " << new_ip << "\n";

    if (!connect_to_server_TCP(new_ip, CLIENT_PORT)) {
        std::cerr << "[ERRO] Falha na conexão TCP com novo primário.\n";
        return false;
    }

    Packet login_pkt = make_packet(CMD, 0, 0, username.size(), username);
    send_packet(login_pkt);

    Packet response = receive_packet();
    std::string response_msg(response._payload, response.length);

    if (response_msg.rfind("ERRO", 0) == 0) {
        std::cerr << "[SERVIDOR] " << response_msg << "\n";
        return false;
    }

    std::cout << "[SERVIDOR] " << response_msg << "\n";
    return true;
}

void server_listener() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CLIENT_NOTIFY_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(sock, (sockaddr*)&addr, sizeof(addr));
    listen(sock, 5);

    //std::cout << "[INFO] Aguardando notificações de novo primário...\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_sock = accept(sock, (sockaddr*)&client_addr, &len);

        char buffer[256];
        ssize_t recv_len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        close(client_sock);  // Fechar assim que receber

        if (recv_len <= 0) continue;

        buffer[recv_len] = '\0';
        std::string msg(buffer);

        if (msg.rfind("NEW_PRIMARY|", 0) == 0) {
            std::string new_ip = msg.substr(12);
            std::cout << "[DEBUG] Novo primário informado: " << new_ip << "\n";

            current_server_ip = new_ip;

            if (reconnect_to_primary(current_server_ip, current_username)) {
                std::cout << "[DEBUG] Reconexão com novo primário bem-sucedida.\n";
            } else {
                std::cerr << "[ERRO] Reconexão com novo primário falhou.\n";
            }
        } else {
            std::cout << "[DEBUG] Mensagem inesperada: " << msg << "\n";
        }
    }
}

void command_loop(const std::string& username) {
    std::string input;
    while (true) {
        std::cout << "[INFO] Digite um comando:\n";
        std::getline(std::cin, input);
        std::istringstream iss(input);
        std::string command;
        iss >> command;

        if (command == "exit") break;
        else if (command == "get_sync_dir") {
            sync_started = get_sync_dir(username);
            start_receiver_thread();
            send_packet(make_packet(CMD, 0, 0, 13, "GET_ALL_FILES"));
        }
        else if (!sync_started) {
            std::cout << "[ERRO] Use get_sync_dir primeiro.\n";
            continue;
        }
        else if (command == "list_client") list_files(get_client_sync_dir_path(), std::cout);
        else if (command == "upload") {
            std::string path;
            std::getline(iss, path);
            path = path.substr(path.find_first_not_of(" \t"));
            upload_file(path, username);
        }
        else if (command == "download") {
            std::string filename;
            std::getline(iss, filename);
            filename = filename.substr(filename.find_first_not_of(" \t"));
            download_file(filename);
        }
        else if (command == "delete") {
            std::string filename;
            std::getline(iss, filename);
            filename = filename.substr(filename.find_first_not_of(" \t"));
            delete_file("sync_dir/" + filename);
        }
        else if (command == "list_server") {
            send_packet(make_packet(CMD, 0, 0, 12, "LIST_SERVER"));
            Packet res = receive_packet();
            std::string result(res._payload, res.length);
            std::cout << result;

        } else if (command == "info") {
            // Envia pacote pedindo para printar info do servidor
            Packet pkt = make_packet(CMD, 0, 0, 0, "INFO");
                send_packet(pkt);

        } else {
            std::cout << "[ERRO] Comando inválido.\n";
        }
    }
}
