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

// Converte tempo UNIX para string legível
std::string format_time(std::time_t t);

// Lista todos os arquivos no caminho especificado
void list_files(const std::string& path, std::ostream& out);

// Deleta o arquivo no caminho especificado
void delete_file(const std::string& filepath);

// Extração do conteúdo de um arquivo no formato de uma string
std::string read_file_content(const std::string& filepath);