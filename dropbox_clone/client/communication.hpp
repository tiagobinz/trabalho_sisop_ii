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

struct Packet;

/*
 * client/communication.hpp
 * Módulo de comunicação no lado do cliente
 */

bool connect_to_server(const std::string& ip, int port);
void send_packet(const Packet& pkt);
Packet receive_packet();

#endif
