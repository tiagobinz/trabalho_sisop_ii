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

#ifndef SYNC_MANAGER_HPP
#define SYNC_MANAGER_HPP

#include <string>

/*
 * sync_manager.hpp
 * Responsável por manter a pasta sync_dir sincronizada com o Servidor
 */

bool get_sync_dir(const std::string& username);

const std::string& get_client_sync_dir_path();

void apply_remote_update(const std::string& payload);

void start_receiver_thread();

#endif
