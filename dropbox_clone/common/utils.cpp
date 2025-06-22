#include "utils.hpp"
#include "../server/service.hpp"

#include <iostream>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <random>

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

std::string join_backups(const std::unordered_map<int, std::string>& backups) {
    std::string result;
    bool first = true;

    for (const auto& [id, ip] : backups) {
        if (!first) {
            result += ",";
        }
        result += std::to_string(id) + ":" + ip;
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

std::string get_local_ip() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    // Permitir reuso de endereço
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in dummy_addr{};
    dummy_addr.sin_family = AF_INET;
    dummy_addr.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &dummy_addr.sin_addr);

    connect(sock, (sockaddr*)&dummy_addr, sizeof(dummy_addr));

    sockaddr_in local_addr{};
    socklen_t addr_len = sizeof(local_addr);
    getsockname(sock, (sockaddr*)&local_addr, &addr_len);

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_addr.sin_addr, ip_str, sizeof(ip_str));

    close(sock);
    return std::string(ip_str);
}

std::string get_client_ip(int client_socket) {
    sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    if (getpeername(client_socket, (sockaddr*)&client_addr, &addr_len) == 0) {
        char client_ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN)) {
            return std::string(client_ip);
        }
    }
    return "UNKNOWN";
}

size_t generate_random_election_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(1000, 999999);
    return dist(gen);
}

int get_port_by_id(int id) {
    int base_port = 8000;
    return base_port + (id % 1000);
}