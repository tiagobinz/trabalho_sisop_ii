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

#ifndef PACKET_HPP
#define PACKET_HPP

#include <string>
#include <cstring>
#include <cstdint>

#define CMD 1
#define DATA 2

#define PAYLOAD_SIZE 1024

/*
 * packet.hpp
 * Definição da estrutura dos pacotes enviados pela rede
 */

typedef struct Packet {
    uint16_t type;        // Tipo do pacote (CMD | DATA)
    uint16_t seqn;        // Número de sequência
    uint32_t total_size;  // Tamanho total do arquivo em bytes
    uint16_t length;      // Comprimento do payload
    char _payload[PAYLOAD_SIZE];  // Dados
} Packet;

Packet make_packet(uint16_t in_type, uint16_t in_seqn, uint32_t in_total_size, uint16_t in_length, std::string in_payload);

#endif
