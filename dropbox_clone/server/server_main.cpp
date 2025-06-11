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
#include "communication.hpp"

int main(int argc, char* argv[]) {
    int port = 12345; // porta padrão

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
        init_server(port, t);

    } else if (server_type == "-b") {
        std::cout << "Iniciando servidor BACKUP na porta " << port << "\n";
        t = ServerType::BACKUP;
        init_server(port, t);

       } else {
           std::cerr << "Erro: tipo de servidor inválido. Use -p para primário ou -b para backup.\n";
           return 1;
       }

    return 0;
}
