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

/*
 * client_main.cpp
 * Ponto de entrada da aplicação Cliente
 */

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

    return 0;
}
