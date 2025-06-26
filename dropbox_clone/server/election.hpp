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

#ifndef ELECTION_HPP
#define ELECTION_HPP

#include <atomic>
#include <mutex>

#include "service.hpp"

#define ELECTION_ANSWER_TIMEOUT 4000
#define ELECTION_COORD_TIMEOUT  4000
#define ELECTION_CHECK_DELAY    200

typedef struct ElectionState {
    std::atomic<bool> election_in_progress{false};
    std::atomic<bool> answer_received{false};
    std::mutex election_mutex;

} ElectionState;

extern ElectionState election_state;


void start_election(const std::unordered_map<int, std::string>& backups);
void become_primary(int this_id, const std::unordered_map<int, std::string>& backups);

#endif
