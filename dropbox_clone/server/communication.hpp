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

#ifndef SERVER_COMMUNICATION_HPP
#define SERVER_COMMUNICATION_HPP

#include <string>
#include <cstdint>
#include <mutex>

#include "service.hpp"

/*
 * server/communication.hpp
 * Módulo de comunicação no lado do servidor
 */
   
std::mutex& get_file_mutex(const std::string& user, const std::string& filename);
bool recv_exact(int socket, void* buffer, size_t length);
std::string receive_full_payload(int client_socket);
void handle_client(int client_socket);

int init_server(int port, ServerType t);

void send_large_payload(int socket, uint16_t type, const std::string& payload);

void register_backup_socket(int backup_socket);

#endif
