#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <memory>

#include "election.hpp"
#include "../common/utils.hpp"

ElectionState election_state;

void start_election(const std::unordered_map<int, std::string>& backups) {
    std::lock_guard<std::mutex> lock(election_state.election_mutex);
    
    election_state.election_in_progress = true;
    election_state.answer_received = false;
    info.election_started = true;
    int this_id = info.election_id;

    std::cout << "[E] Iniciando eleição Bully. Meu ID: " << this_id << "\n";

    bool send_to_someone = false;

    // Envia ELECTION para todos com ID maior
    for (const auto& [backup_id, ip] : info.backups) {

        if ((backup_id > this_id) && (backup_id != this_id)) {

            int sock = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(get_port_by_id(backup_id));
            inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

            std::cout << "[E] Tentando conectar em ID " << backup_id << " na porta " << get_port_by_id(backup_id) << "\n";
            if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {

                std::string election_msg = "ELECTION|" + std::to_string(this_id);
                if (send(sock, election_msg.c_str(), election_msg.size(), 0) > 0) {

                    std::cout << "[E] Enviado ELECTION para ID " << backup_id << "\n";
                    send_to_someone = true;
                }
            } else {
                std::cerr << "[ERRO] Falha ao conectar no backup ID " << backup_id << "\n";
            }
            //close(sock);
        }
    }

    if(!send_to_someone) {
        become_primary(this_id, info.backups);
        return;

    } else {

        std::cout << "[E] Aguardando ANSWER...\n";

        using clock = std::chrono::steady_clock;
        auto start_time = clock::now();
        const auto timeout = std::chrono::milliseconds(ELECTION_ANSWER_TIMEOUT);

        while(clock::now() - start_time <= timeout) {

            // Alguém respondeu, então eu não sou o líder
            if (election_state.answer_received.load()) {
                std::cout << "[E] Recebi ANSWER. Aguardando novo coordenador...\n";
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(ELECTION_CHECK_DELAY));
        }
        std::cout << "[E] Nenhuma resposta após " << ELECTION_ANSWER_TIMEOUT << "ms. Eu serei o Primário.\n";
        become_primary(this_id, backups);
    }
}

// Função auxiliar para tornar-se primário
void become_primary(int this_id, const std::unordered_map<int, std::string>& backups) {
    std::lock_guard<std::mutex> lock(election_state.election_mutex);
    
    info.election_started = false;
    election_state.election_in_progress = false;

    // Envia COORDINATOR para todos os backups
    std::string coordinator_msg = "COORDINATOR|" + std::to_string(this_id);
    
    std::cout << "[E] Enviando COORDINATOR para todos os backups...\n";
    
    for (const auto& [backup_id, ip] : backups) {
        if (backup_id != this_id) { // Enviar para todos exceto a si mesmo
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) continue;

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(get_port_by_id(backup_id));
            inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

            if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
                send(sock, coordinator_msg.c_str(), coordinator_msg.size(), 0);
                std::cout << "[E] COORDINATOR enviado para ID " << backup_id << " (" << ip << ")\n";
            } else {
                std::cout << "[AVISO] Falha ao enviar COORDINATOR para " << backup_id << " (" << ip << ")\n";
            }
            close(sock);
        }
    }

    // Atualiza dados internos
    info.primary_ip = info.backups[this_id];
    info.type = ServerType::PRIMARY;
    
    std::cout << "[E] *** AGORA SOU O SERVIDOR PRIMÁRIO! ***\n";
}