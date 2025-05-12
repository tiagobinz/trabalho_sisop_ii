#ifndef FILE_TRANSFER_SERVICE_H
#define FILE_TRANSFER_SERVICE_H

#include <string>
#include <functional>
#include <cstdint>
#include <vector>

/**
 * @class FileTransferService
 * @brief Serviço para transferência de arquivos via socket
 * 
 * Esta classe provê métodos para enviar e receber arquivos através de conexões de socket.
 * Pode ser utilizada tanto pelo cliente quanto pelo servidor.
 */
class FileTransferService {
public:
    /**
     * @brief Construtor que inicializa o serviço com um socket já existente
     * @param socket_fd Socket file descriptor para comunicação
     */
    FileTransferService(int socket_fd);
    
    /**
     * @brief Destrutor da classe
     */
    ~FileTransferService();
    
    /**
     * @brief Envia um arquivo para o socket conectado
     * @param filePath Caminho do arquivo a ser enviado
     * @param progressCallback Função opcional para reportar progresso
     * @return true se o arquivo foi enviado com sucesso, false caso contrário
     */
    bool sendFile(const std::string& filePath, 
                  std::function<void(size_t, size_t)> progressCallback = nullptr);
    
    /**
     * @brief Recebe um arquivo do socket conectado
     * @param saveDirectory Diretório onde o arquivo será salvo
     * @param progressCallback Função opcional para reportar progresso
     * @return Nome do arquivo recebido ou string vazia em caso de erro
     */
    std::string receiveFile(const std::string& saveDirectory,
                            std::function<void(size_t, size_t)> progressCallback = nullptr);
    
    /**
     * @brief Envia uma mensagem simples via socket
     * @param message Mensagem a ser enviada
     * @return true se a mensagem foi enviada com sucesso, false caso contrário
     */
    bool sendMessage(const std::string& message);
    
    /**
     * @brief Recebe uma mensagem simples via socket
     * @return Mensagem recebida ou string vazia em caso de erro
     */
    std::string receiveMessage();

private:
    int socket_fd;                   // Socket file descriptor
    static const size_t BUFFER_SIZE = 8192;  // Tamanho do buffer para transferência
    
    /**
     * @brief Envia dados binários via socket
     * @param data Ponteiro para os dados a serem enviados
     * @param size Tamanho dos dados em bytes
     * @return true se os dados foram enviados com sucesso, false caso contrário
     */
    bool sendData(const void* data, size_t size);
    
    /**
     * @brief Recebe dados binários via socket
     * @param buffer Buffer para armazenar os dados recebidos
     * @param size Tamanho máximo de dados a receber
     * @return Número de bytes recebidos ou -1 em caso de erro
     */
    ssize_t receiveData(void* buffer, size_t size);
    
    /**
     * @brief Envia o cabeçalho de transferência de arquivo (nome e tamanho)
     * @param fileName Nome do arquivo a ser enviado
     * @param fileSize Tamanho do arquivo em bytes
     * @return true se o cabeçalho foi enviado com sucesso, false caso contrário
     */
    bool sendFileHeader(const std::string& fileName, uint32_t fileSize);
    
    /**
     * @brief Recebe o cabeçalho de transferência de arquivo
     * @param fileName Referência para armazenar o nome do arquivo recebido
     * @param fileSize Referência para armazenar o tamanho do arquivo
     * @return true se o cabeçalho foi recebido com sucesso, false caso contrário
     */
    bool receiveFileHeader(std::string& fileName, uint32_t& fileSize);
};

#endif // FILE_TRANSFER_SERVICE_H