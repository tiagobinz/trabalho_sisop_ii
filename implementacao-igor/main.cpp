#include "FileTransferService.h"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <signal.h>
#include <filesystem>

// Função para exibir mensagem de progresso
void showProgress(size_t current, size_t total, const std::string& prefix) {
    float progress = (float)current / total * 100.0f;
    std::cout << "\r" << prefix << ": " << (int)progress << "% (" 
              << current << "/" << total << " bytes)    " << std::flush;
}

// SERVIDOR
class Server {
private:
    int server_fd;
    int port;
    bool running;
    std::vector<std::thread> client_threads;

public:
    Server(int port) : port(port), running(true) {
        // Inicializa o socket do servidor
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw std::runtime_error("Erro ao criar socket do servidor");
        }

        // Permite reutilizar o endereço
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // Configura o endereço do servidor
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);
        memset(&server_addr.sin_zero, 0, sizeof(server_addr.sin_zero));

        // Associa o socket ao endereço
        if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(server_fd);
            throw std::runtime_error("Erro ao vincular socket ao endereço");
        }

        // Coloca o socket em modo de escuta
        if (listen(server_fd, 5) < 0) {
            close(server_fd);
            throw std::runtime_error("Erro ao colocar socket em modo de escuta");
        }

        // Cria diretório para arquivos
        mkdir("server_files", 0777);
    }

    ~Server() {
        stop();
    }

    void stop() {
        running = false;
        if (server_fd >= 0) {
            close(server_fd);
            server_fd = -1;
        }

        // Aguarda o término de todas as threads
        for (auto& thread : client_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        client_threads.clear();
    }

    void handleClient(int client_fd, sockaddr_in client_addr) {
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);
        std::cout << "[SERVER] Nova conexão de " << client_ip << ":" << client_port << std::endl;

        // Cria o serviço de transferência para este cliente
        FileTransferService service(client_fd);

        // Recebe nome do cliente
        std::string clientName = service.receiveMessage();
        std::cout << "[SERVER] Cliente identificado como: " << clientName << std::endl;

        // Cria diretório específico para este cliente
        std::string clientDir = "server_files/" + clientName;
        mkdir(clientDir.c_str(), 0777);

        // Loop de comunicação com o cliente
        while (true) {
            // Aguarda um comando do cliente
            std::string command = service.receiveMessage();
            if (command.empty()) {
                std::cout << "[SERVER] Cliente " << clientName << " desconectou." << std::endl;
                break;
            }

            std::cout << "[SERVER] Comando recebido de " << clientName << ": " << command << std::endl;

            // Processa o comando
            if (command == "UPLOAD") {
                // Cliente quer enviar um arquivo para o servidor
                auto progressCallback = [&clientName](size_t current, size_t total) {
                    showProgress(current, total, "[SERVER] Recebendo de " + clientName);
                };

                // Recebe o arquivo
                std::string fileName = service.receiveFile(clientDir, progressCallback);
                std::cout << std::endl;

                if (!fileName.empty()) {
                    std::cout << "[SERVER] Arquivo recebido de " << clientName << ": " << fileName << std::endl;
                    service.sendMessage("OK");
                } else {
                    std::cout << "[SERVER] Falha ao receber arquivo de " << clientName << std::endl;
                    service.sendMessage("ERRO");
                }
            }
            else if (command == "DOWNLOAD") {
                // Cliente quer baixar um arquivo do servidor
                // Primeiro recebe o nome do arquivo
                std::string fileName = service.receiveMessage();
                std::string filePath = clientDir + "/" + fileName;

                // Verifica se o arquivo existe
                if (!std::filesystem::exists(filePath)) {
                    std::cout << "[SERVER] Arquivo não encontrado: " << filePath << std::endl;
                    service.sendMessage("ARQUIVO_NAO_ENCONTRADO");
                    continue;
                }

                // Confirma que vai enviar o arquivo
                service.sendMessage("ENVIANDO_ARQUIVO");

                // Função para acompanhar o progresso do envio
                auto progressCallback = [&clientName](size_t current, size_t total) {
                    showProgress(current, total, "[SERVER] Enviando para " + clientName);
                };

                // Envia o arquivo
                bool success = service.sendFile(filePath, progressCallback);
                std::cout << std::endl;

                if (success) {
                    std::cout << "[SERVER] Arquivo enviado para " << clientName << ": " << fileName << std::endl;
                } else {
                    std::cout << "[SERVER] Falha ao enviar arquivo para " << clientName << ": " << fileName << std::endl;
                }
            }
            else if (command == "LIST") {
                // Cliente quer listar arquivos disponíveis
                std::string fileList;

                try {
                    for (const auto& entry : std::filesystem::directory_iterator(clientDir)) {
                        if (entry.is_regular_file()) {
                            fileList += entry.path().filename().string() + "\n";
                        }
                    }
                } catch (const std::exception& e) {
                    fileList = "Erro ao listar arquivos: " + std::string(e.what());
                }

                if (fileList.empty()) {
                    fileList = "Nenhum arquivo encontrado.";
                }

                service.sendMessage(fileList);
            }
            else if (command == "EXIT") {
                // Cliente quer encerrar a conexão
                service.sendMessage("GOODBYE");
                break;
            }
            else {
                // Comando desconhecido
                service.sendMessage("COMANDO_DESCONHECIDO");
            }
        }

        // Fecha o socket do cliente
        close(client_fd);
    }

    void start() {
        std::cout << "[SERVER] Servidor iniciado na porta " << port << std::endl;

        while (running) {
            // Configura timeout para accept
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(server_fd, &readfds);

            // Timeout de 1 segundo para permitir verificação periódica de 'running'
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int activity = select(server_fd + 1, &readfds, NULL, NULL, &tv);

            if (activity < 0 && errno != EINTR) {
                perror("[SERVER] Erro em select");
                break;
            }

            if (!running) break;

            // Se não há atividade, continua o loop
            if (activity == 0) continue;

            // Verifica se há nova conexão
            if (FD_ISSET(server_fd, &readfds)) {
                sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) {
                    perror("[SERVER] Erro ao aceitar conexão");
                    continue;
                }

                // Cria uma nova thread para atender o cliente
                client_threads.emplace_back(&Server::handleClient, this, client_fd, client_addr);
                client_threads.back().detach();
            }
        }

        std::cout << "[SERVER] Servidor encerrado" << std::endl;
    }
};

// CLIENTE
class Client {
private:
    int sock;
    std::string clientName;
    FileTransferService* service;

public:
    Client(const std::string& name, const std::string& serverIp, int serverPort) 
        : clientName(name), sock(-1), service(nullptr) {
        
        // Cria o socket
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            throw std::runtime_error("Erro ao criar socket do cliente");
        }

        // Configura o endereço do servidor
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(serverPort);
        memset(&server_addr.sin_zero, 0, sizeof(server_addr.sin_zero));

        // Converte o endereço IP
        if (inet_pton(AF_INET, serverIp.c_str(), &server_addr.sin_addr) <= 0) {
            close(sock);
            throw std::runtime_error("Endereço IP inválido");
        }

        // Conecta ao servidor
        if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sock);
            throw std::runtime_error("Falha ao conectar ao servidor");
        }

        std::cout << "[CLIENT] Conectado ao servidor em " << serverIp << ":" << serverPort << std::endl;

        // Cria o serviço de transferência
        service = new FileTransferService(sock);

        // Cria diretório local para arquivos
        mkdir("client_files", 0777);

        // Envia nome do cliente ao servidor
        service->sendMessage(clientName);
    }

    ~Client() {
        if (service) {
            delete service;
        }
        if (sock >= 0) {
            close(sock);
        }
    }

    void uploadFile(const std::string& filePath) {
        // Verifica se o arquivo existe
        if (!std::filesystem::exists(filePath)) {
            std::cout << "[CLIENT] Arquivo não encontrado: " << filePath << std::endl;
            return;
        }

        // Avisa o servidor que vai enviar um arquivo
        service->sendMessage("UPLOAD");

        // Função para acompanhar o progresso do envio
        auto progressCallback = [](size_t current, size_t total) {
            showProgress(current, total, "[CLIENT] Enviando");
        };

        // Envia o arquivo
        bool success = service->sendFile(filePath, progressCallback);
        std::cout << std::endl;

        if (success) {
            // Aguarda confirmação do servidor
            std::string response = service->receiveMessage();
            if (response == "OK") {
                std::cout << "[CLIENT] Arquivo enviado com sucesso!" << std::endl;
            } else {
                std::cout << "[CLIENT] Servidor reportou erro: " << response << std::endl;
            }
        } else {
            std::cout << "[CLIENT] Falha ao enviar arquivo" << std::endl;
        }
    }

    void downloadFile(const std::string& fileName) {
        // Solicita download de arquivo ao servidor
        service->sendMessage("DOWNLOAD");

        // Envia o nome do arquivo desejado
        service->sendMessage(fileName);

        // Aguarda resposta do servidor
        std::string response = service->receiveMessage();
        if (response == "ARQUIVO_NAO_ENCONTRADO") {
            std::cout << "[CLIENT] Arquivo não encontrado no servidor" << std::endl;
            return;
        }

        if (response != "ENVIANDO_ARQUIVO") {
            std::cout << "[CLIENT] Erro inesperado do servidor: " << response << std::endl;
            return;
        }

        // Função para acompanhar o progresso do download
        auto progressCallback = [](size_t current, size_t total) {
            showProgress(current, total, "[CLIENT] Recebendo");
        };

        // Recebe o arquivo
        std::string receivedFileName = service->receiveFile("client_files", progressCallback);
        std::cout << std::endl;

        if (!receivedFileName.empty()) {
            std::cout << "[CLIENT] Arquivo recebido: " << receivedFileName << std::endl;
        } else {
            std::cout << "[CLIENT] Falha ao receber arquivo" << std::endl;
        }
    }

    void listFiles() {
        // Solicita lista de arquivos ao servidor
        service->sendMessage("LIST");

        // Recebe e exibe a lista
        std::string fileList = service->receiveMessage();
        std::cout << "\n=== Arquivos no servidor ===" << std::endl;
        std::cout << fileList << std::endl;
        std::cout << "===========================" << std::endl;
    }

    void exit() {
        service->sendMessage("EXIT");
        std::string response = service->receiveMessage();
        std::cout << "[CLIENT] " << response << std::endl;
    }

    void showMenu() {
        std::cout << "\n======== MENU ========" << std::endl;
        std::cout << "1. Enviar arquivo para o servidor" << std::endl;
        std::cout << "2. Baixar arquivo do servidor" << std::endl;
        std::cout << "3. Listar arquivos no servidor" << std::endl;
        std::cout << "4. Sair" << std::endl;
        std::cout << "======================" << std::endl;
        std::cout << "Escolha uma opção: ";
    }

    void start() {
        std::string input;
        
        while (true) {
            showMenu();
            std::getline(std::cin, input);

            if (input == "1") {
                std::cout << "Digite o caminho do arquivo: ";
                std::getline(std::cin, input);
                uploadFile(input);
            }
            else if (input == "2") {
                std::cout << "Digite o nome do arquivo: ";
                std::getline(std::cin, input);
                downloadFile(input);
            }
            else if (input == "3") {
                listFiles();
            }
            else if (input == "4") {
                exit();
                break;
            }
            else {
                std::cout << "Opção inválida." << std::endl;
            }
        }
    }
};

// Manipulador de sinais para encerramento gracioso
Server* globalServer = nullptr;

void signalHandler(int signum) {
    std::cout << "\nSinal recebido: " << signum << std::endl;
    if (globalServer) {
        globalServer->stop();
    }
}

int main(int argc, char* argv[]) {
    // Verifica argumentos
    if (argc < 2) {
        std::cout << "Uso: " << argv[0] << " [server|client]" << std::endl;
        std::cout << "  Para servidor: " << argv[0] << " server [porta]" << std::endl;
        std::cout << "  Para cliente: " << argv[0] << " client [nome] [ip] [porta]" << std::endl;
        return 1;
    }

    std::string mode = argv[1];

    try {
        if (mode == "server") {
            // Modo servidor
            int port = (argc > 2) ? std::stoi(argv[2]) : 8080;
            
            Server server(port);
            
            // Configura o manipulador de sinais
            globalServer = &server;
            signal(SIGINT, signalHandler);
            signal(SIGTERM, signalHandler);
            
            server.start();
        }
        else if (mode == "client") {
            // Modo cliente
            if (argc < 5) {
                std::cout << "Uso para cliente: " << argv[0] << " client [nome] [ip] [porta]" << std::endl;
                return 1;
            }
            
            std::string name = argv[2];
            std::string ip = argv[3];
            int port = std::stoi(argv[4]);
            
            Client client(name, ip, port);
            client.start();
        }
        else {
            std::cout << "Modo inválido. Use 'server' ou 'client'." << std::endl;
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Erro: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}