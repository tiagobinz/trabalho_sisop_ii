#include "utils.hpp"
#include "../server/service.hpp"

#include <iostream>
#include <sys/stat.h>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>

std::string format_time(std::time_t t) {
    char buf[64];
    std::strftime(buf, sizeof(buf), "%F %T", std::localtime(&t));
    return buf;
}

void list_files(const std::string& path, std::ostream& out) {
    if (!std::filesystem::exists(path)) {
        std::cerr << "[ERRO] Diretório de sincronização não existe: " << path << "\n";
        return;
    } else if (std::filesystem::is_empty(path)) {
        out << "Diretório de sincronização vazio.\n";
        return;
    }

    out << std::left << std::setw(45) << "Arquivo"
              << std::setw(25) << "Modificado (mtime)"
              << std::setw(25) << "Acessado (atime)"
              << std::setw(25) << "Alterado (ctime)"
              << "\n";

    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (!entry.is_regular_file()) continue;

        std::string name = entry.path().filename().string();
        struct stat st;
        if (stat(entry.path().c_str(), &st) == 0) {
            out << std::setw(45) << name
                      << std::setw(25) << format_time(st.st_mtime)
                      << std::setw(25) << format_time(st.st_atime)
                      << std::setw(25) << format_time(st.st_ctime)
                      << "\n";
        }
    }
}

void delete_file(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        std::cerr << "[ERRO] Arquivo não encontrado: " << filepath << "\n";
        return;
    }

    try {
        std::filesystem::remove(filepath);
        std::cout << "[INFO] Arquivo removido localmente: " << filepath << "\n";

    } catch (const std::exception& e) {
        std::cerr << "[ERRO] Falha ao remover o arquivo: " << e.what() << "\n";
    }
}

std::string read_file_content(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string join_backups(const std::unordered_map<std::string, int>& backups) {
    std::string result;
    bool first = true;

    for (const auto& [ip, id] : backups) {
        if (!first) {
            result += ",";
        }
        result += ip + ":" + std::to_string(id);
        first = false;
    }
    return result;
}


bool recv_exact(int socket, void* buffer, size_t length) {
    size_t total_read = 0;
    while (total_read < length) {
        ssize_t n = recv(socket, (char*)buffer + total_read, length - total_read, 0);
        if (n <= 0) return false;
        total_read += n;
    }
    return true;
}