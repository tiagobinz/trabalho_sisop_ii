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

#ifndef CLIENT_COMMUNICATION_HPP
#define CLIENT_COMMUNICATION_HPP

#include <string>
#include <cstdint>

struct Packet;

/*
 * client/communication.hpp
 * Módulo de comunicação no lado do cliente
 */

bool connect_to_server_TCP(const std::string& ip, int port);
void send_packet(const Packet& pkt);
Packet receive_packet();
void send_large_payload(uint16_t type, const std::string& payload);
std::string receive_full_payload();

// Solicita download e salva o arquivo
void download_file(const std::string& filename);

// Envia o conteúdo do arquivo informado para o servidor
void upload_file(const std::string& full_path, const std::string& username = "");

// Solicita que um arquivo seja deletado no servidor
void delete_file_on_server(const std::string& filename);

#endif
