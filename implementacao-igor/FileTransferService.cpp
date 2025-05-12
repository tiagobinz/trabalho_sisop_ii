#include "FileTransferService.h"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <filesystem>
#include <algorithm>

FileTransferService::FileTransferService(int socket_fd) : socket_fd(socket_fd) {
    // Nada a fazer aqui além de inicializar o socket_fd
}

FileTransferService::~FileTransferService() {
    // Não fechamos o socket_fd aqui, pois ele pode ser usado em outros lugares
    // O fechamento é responsabilidade de quem criou o socket
}

bool FileTransferService::sendFile(const std::string& filePath, 
                                 std::function<void(size_t, size_t)> progressCallback) {
    // Verifica se o arquivo existe
    struct stat fileInfo;
    if (stat(filePath.c_str(), &fileInfo) != 0) {
        std::cerr << "[ERROR] Não foi possível obter informações do arquivo: " << filePath << std::endl;
        perror("Erro");
        return false;
    }
    
    uint32_t fileSize = fileInfo.st_size;
    std::string fileName = std::filesystem::path(filePath).filename().string();
    
    // Envia o cabeçalho do arquivo
    if (!sendFileHeader(fileName, fileSize)) {
        return false;
    }
    
    // Abre o arquivo para leitura
    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "[ERROR] Não foi possível abrir o arquivo: " << filePath << std::endl;
        perror("Erro");
        return false;
    }
    
    // Envia o conteúdo do arquivo em blocos
    char buffer[BUFFER_SIZE];
    size_t totalSent = 0;
    bool success = true;
    
    while (totalSent < fileSize) {
        ssize_t bytesRead = read(fd, buffer, BUFFER_SIZE);
        if (bytesRead <= 0) {
            if (bytesRead < 0) {
                perror("[ERROR] Erro ao ler arquivo");
            }
            success = false;
            break;
        }
        
        if (!sendData(buffer, bytesRead)) {
            std::cerr << "[ERROR] Falha ao enviar dados do arquivo" << std::endl;
            success = false;
            break;
        }
        
        totalSent += bytesRead;
        
        // Atualiza o progresso se um callback foi fornecido
        if (progressCallback) {
            progressCallback(totalSent, fileSize);
        }
    }
    
    close(fd);
    
    // Aguarda confirmação
    if (success) {
        std::string confirmation = receiveMessage();
        if (confirmation != "OK") {
            std::cerr << "[ERROR] Confirmação inválida recebida: " << confirmation << std::endl;
            success = false;
        }
    }
    
    return success;
}

std::string FileTransferService::receiveFile(const std::string& saveDirectory,
                                           std::function<void(size_t, size_t)> progressCallback) {
    // Verifica se o diretório existe, cria se necessário
    if (access(saveDirectory.c_str(), F_OK) != 0) {
        if (mkdir(saveDirectory.c_str(), 0777) != 0) {
            std::cerr << "[ERROR] Não foi possível criar o diretório: " << saveDirectory << std::endl;
            perror("Erro");
            return "";
        }
    }
    
    // Recebe o cabeçalho do arquivo
    std::string fileName;
    uint32_t fileSize;
    
    if (!receiveFileHeader(fileName, fileSize)) {
        return "";
    }
    
    // Sanitiza o nome do arquivo
    fileName.erase(std::remove(fileName.begin(), fileName.end(), '\n'), fileName.end());
    fileName.erase(std::remove(fileName.begin(), fileName.end(), '\r'), fileName.end());
    
    // Valida o nome do arquivo
    if (fileName.empty() || fileName.find('/') != std::string::npos) {
        std::cerr << "[ERROR] Nome de arquivo inválido: \"" << fileName << "\"" << std::endl;
        return "";
    }
    
    // Caminho completo para o arquivo
    std::string fullPath = saveDirectory + "/" + fileName;
    
    // Abre o arquivo para escrita
    int fd = open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        std::cerr << "[ERROR] Não foi possível criar arquivo para escrita: " << fullPath << std::endl;
        perror("Erro");
        return "";
    }
    
    // Recebe o conteúdo do arquivo
    char buffer[BUFFER_SIZE];
    size_t totalReceived = 0;
    bool success = true;
    
    while (totalReceived < fileSize) {
        size_t remaining = fileSize - totalReceived;
        size_t toRead = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
        
        ssize_t bytesReceived = receiveData(buffer, toRead);
        if (bytesReceived <= 0) {
            std::cerr << "[ERROR] Erro ao receber dados do arquivo" << std::endl;
            success = false;
            break;
        }
        
        ssize_t written = write(fd, buffer, bytesReceived);
        if (written != bytesReceived) {
            std::cerr << "[ERROR] Erro ao escrever dados no arquivo" << std::endl;
            perror("Erro");
            success = false;
            break;
        }
        
        totalReceived += bytesReceived;
        
        // Atualiza o progresso se um callback foi fornecido
        if (progressCallback) {
            progressCallback(totalReceived, fileSize);
        }
    }
    
    // Finaliza o arquivo
    fsync(fd);
    close(fd);
    
    if (!success || totalReceived < fileSize) {
        std::cerr << "[ERROR] Transferência incompleta: " << totalReceived << "/" << fileSize << " bytes" << std::endl;
        unlink(fullPath.c_str()); // Remove o arquivo incompleto
        return "";
    }
    
    // Envia confirmação
    sendMessage("OK");
    
    return fileName;
}

bool FileTransferService::sendMessage(const std::string& message) {
    size_t messageLen = message.size();
    uint32_t messageSize = htonl(static_cast<uint32_t>(messageLen));
    
    // Envia o tamanho da mensagem
    if (!sendData(&messageSize, sizeof(messageSize))) {
        return false;
    }
    
    // Envia o conteúdo da mensagem
    return sendData(message.c_str(), messageLen);
}

std::string FileTransferService::receiveMessage() {
    uint32_t messageSize = 0;
    
    // Recebe o tamanho da mensagem
    if (receiveData(&messageSize, sizeof(messageSize)) != sizeof(messageSize)) {
        return "";
    }
    
    messageSize = ntohl(messageSize);
    if (messageSize > BUFFER_SIZE * 10) { // Limita o tamanho da mensagem
        std::cerr << "[ERROR] Tamanho de mensagem inválido: " << messageSize << std::endl;
        return "";
    }
    
    // Aloca buffer para a mensagem
    std::vector<char> buffer(messageSize + 1, 0);
    
    // Recebe o conteúdo da mensagem
    if (receiveData(buffer.data(), messageSize) != messageSize) {
        return "";
    }
    
    return std::string(buffer.data(), messageSize);
}

bool FileTransferService::sendData(const void* data, size_t size) {
    size_t totalSent = 0;
    const char* buffer = static_cast<const char*>(data);
    
    while (totalSent < size) {
        ssize_t sent = send(socket_fd, buffer + totalSent, size - totalSent, 0);
        if (sent <= 0) {
            if (sent < 0) {
                perror("[ERROR] Erro ao enviar dados");
            }
            return false;
        }
        totalSent += sent;
    }
    
    return true;
}

ssize_t FileTransferService::receiveData(void* buffer, size_t size) {
    return recv(socket_fd, buffer, size, MSG_WAITALL);
}

bool FileTransferService::sendFileHeader(const std::string& fileName, uint32_t fileSize) {
    // Envia o nome do arquivo
    if (!sendMessage(fileName)) {
        std::cerr << "[ERROR] Falha ao enviar nome do arquivo" << std::endl;
        return false;
    }
    
    // Envia o tamanho do arquivo
    uint32_t fileSizeNet = htonl(fileSize);
    if (!sendData(&fileSizeNet, sizeof(fileSizeNet))) {
        std::cerr << "[ERROR] Falha ao enviar tamanho do arquivo" << std::endl;
        return false;
    }
    
    return true;
}

bool FileTransferService::receiveFileHeader(std::string& fileName, uint32_t& fileSize) {
    // Recebe o nome do arquivo
    fileName = receiveMessage();
    if (fileName.empty()) {
        std::cerr << "[ERROR] Falha ao receber nome do arquivo" << std::endl;
        return false;
    }
    
    // Recebe o tamanho do arquivo
    uint32_t fileSizeNet;
    if (receiveData(&fileSizeNet, sizeof(fileSizeNet)) != sizeof(fileSizeNet)) {
        std::cerr << "[ERROR] Falha ao receber tamanho do arquivo" << std::endl;
        return false;
    }
    
    fileSize = ntohl(fileSizeNet);
    return true;
}