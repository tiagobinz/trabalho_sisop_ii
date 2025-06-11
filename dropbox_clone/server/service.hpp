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

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <string>

/*
 * server/service.hpp
 * Módulo de serviços no lado servidor para a replicação passiva
 */

#define MAX_SERVER_MULTICAST    15

void multicast_primary_info(int multicast_port);

std::string listen_for_primary_multicast(int multicast_port);

std::string get_local_ip();

#endif