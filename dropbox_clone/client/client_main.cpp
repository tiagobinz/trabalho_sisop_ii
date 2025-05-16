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
#include "communication.hpp"
#include "sync_manager.hpp"
#include "../common/packet.hpp"
#include "../common/utils.hpp"

/*
 * client_main.cpp
 * Ponto de entrada da aplicação Cliente
 */

// Loop principal do cliente, que recebe os comandos do usuário
void command_loop(const std::string& username);

// Solicita download e salva o arquivo
void download_file(const std::string& filename);

// Envia o conteúdo do arquivo informado para o servidor
void upload_file(const std::string& full_path);

// Alterado para true após a execução de "get_sync_dir"
static bool sync_started = false;

int main(int argc, char* argv[]) {
    /*
     * Um cliente deve poder estabelecer uma sessão com o servidor via linha de comando utilizando:
     * ./myClient <username> <server_ip_address> <port>, onde:
     * <username> representa o identificador do usuário
     * <server_ip_address> representa o endereço IP do servidor
     * <port> representa a porta
     */
    if (argc != 4) {
        std::cerr << "Uso: ./myClient <username> <server_ip> <port>\n";
        return 1;
    }
    
    // Extração dos argumentos
    std::string username = argv[1];
    std::string server_ip = argv[2];
    int port = std::stoi(argv[3]);

    // Impressão dos argumentos
    std::cout << "username = " << username << std::endl;
    std::cout << "server_ip = " << server_ip << std::endl;
    std::cout << "port = " << port << std::endl;

    // Conexão com servidor
    if (!connect_to_server(server_ip, port)) {
        std::cerr << "Erro ao conectar com o servidor.\n";
        return 1;
    }
    std::cout << "Conexão com servidor bem-sucedida.\n";

    // Envia pacote de login
    Packet login_pkt = make_packet(CMD, 0, 0, username.size(), username);
    send_packet(login_pkt);
    std::cout << "Login enviado: " << username << "\n";

    // Espera resposta
    Packet response = receive_packet();
    std::string response_msg(response._payload, response.length);

    // Checa se houve algum erro
    if (response_msg.rfind("ERRO", 0) == 0) {
        std::cerr << "[SERVIDOR] " << response_msg << "\n";
        return 1; // Encerra execução com erro
    }
    else {
        std::cout << "[SERVIDOR] " << response_msg << "\n";
    }

    // Começa a receber comandos do usuário
    command_loop(username);

    return 0;
}

void command_loop(const std::string& username) {
    std::string input;

    std::cout << "[INFO] Cliente pronto. Digite um comando:\n";

    while (true) {
        // Espera entrada de um comando
        std::getline(std::cin, input);
        std::istringstream iss(input);

        // Extrai o nome do comando
        std::string command, arg;
        iss >> command;

        // Comando "exit"
        if (command == "exit") {
            std::cout << "Encerrando sessão...\n";
            break;
        }

        // Comando "get_sync_dir"
        else if (command == "get_sync_dir") {
            // Create the sync_dir
            sync_started = get_sync_dir(username);

            // Start the thread that receives updates from the server
            start_receiver_thread();

            // Request the existing files on the server
            std::string command = "GET_ALL_FILES";
            Packet request = make_packet(CMD, 0, 0, command.size(), command);
            send_packet(request);
        }

        // Outros comandos dependem de iniciar a sincronização antes
        else if (sync_started) {
            // Comando "list_client"
            if (command == "list_client") {
                list_files(get_client_sync_dir_path(), std::cout);
            }

            // Comando "upload"
            else if (command == "upload") {
                std::string path;
                std::getline(iss, path);
                path = path.substr(path.find_first_not_of(" \t"));
            
                if (path.empty()) {
                    std::cout << "[ERRO] Especifique o caminho do arquivo para upload.\n";
                    continue;
                }
            
                upload_file(path);
            }

            // Comando "download"
            else if (command == "download") {
                std::string filename;
                std::getline(iss, filename); // lê o resto da linha como um único argumento
                filename = filename.substr(filename.find_first_not_of(" \t")); // remove espaços à esquerda
            
                if (filename.empty()) {
                    std::cout << "[ERRO] Especifique o nome do arquivo para download.\n";
                    continue;
                }
            
                std::cout << "Vamos baixar " << filename << std::endl;
                download_file(filename);
            }

            // Comando "delete"
            else if (command == "delete") {
                std::string filename;
                std::getline(iss, filename);
                filename = filename.substr(filename.find_first_not_of(" \t"));
            
                if (filename.empty()) {
                    std::cout << "[ERRO] Especifique o nome do arquivo para deletar.\n";
                    continue;
                }
            
                delete_file(filename);
            }

            // Comando "list_server"
            else if (command == "list_server") {
                // Envia pacote perguntando os arquivos do servidor
                Packet pkt = make_packet(CMD, 0, 0, 0, "LIST_SERVER");
                send_packet(pkt);
                
                // Recebe e imprime o resultado
                Packet response = receive_packet();
                std::string result(response._payload, response.length);
                std::cout << result;
            }

            // Comando inválido
            else {
                std::cout << "[ERRO] Comando não reconhecido.\n";
            }
        }
        else {
            std::cout << "[ERRO] Sincronização ainda não iniciada. Utilize o comando get_sync_dir.\n";
        }
    }
}

void download_file(const std::string& filename) {
    std::string command = "DOWNLOAD\n" + filename;
    Packet request = make_packet(CMD, 0, 0, command.size(), command);
    send_packet(request);
    
    std::string payload = receive_full_payload();

    if (payload.rfind("UPLOAD ", 0) == 0) {
        size_t newline_pos = payload.find('\n');
        if (newline_pos != std::string::npos) {
            std::string header = payload.substr(0, newline_pos);
            std::string content = payload.substr(newline_pos + 1);
            std::string filename = header.substr(7); // após "UPLOAD "

            std::ofstream file(filename, std::ios::binary);
            file << content;
            file.close();

            std::cout << "[INFO] Arquivo '" << filename << "' salvo localmente.\n";
        } else {
            std::cerr << "[ERRO] Resposta mal formatada do servidor.\n";
        }
    } else {
        std::cerr << "[ERRO] Arquivo não encontrado no servidor.\n";
    }
}

void upload_file(const std::string& full_path) {
    if (!std::filesystem::exists(full_path)) {
        std::cerr << "[ERRO] Arquivo não encontrado: " << full_path << "\n";
        return;
    }

    std::string filename = std::filesystem::path(full_path).filename().string();

    std::ifstream file(full_path, std::ios::binary);
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    std::string full_command = "UPLOAD\n" + filename + "\n" + content;
    send_large_payload(CMD, full_command);

    std::cout << "[INFO] Arquivo '" << filename << "' enviado ao servidor.\n";
}
