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
 * utils.hpp
 * Funções úteis para todos os módulos
 */

 #include <string>
 #include <chrono>
 #include <vector>
 #include <unordered_map>

// Converte tempo UNIX para string legível
std::string format_time(std::time_t t);

// Lista todos os arquivos no caminho especificado
void list_files(const std::string& path, std::ostream& out);

// Deleta o arquivo no caminho especificado
void delete_file(const std::string& filepath);

// Extração do conteúdo de um arquivo no formato de uma string
std::string read_file_content(const std::string& filepath);

std::string join_backups(const std::unordered_map<int, std::string>& backups);

bool recv_exact(int socket, void* buffer, size_t length);
std::string get_local_ip();
std::string get_client_ip(int client_socket);
size_t generate_random_election_id();