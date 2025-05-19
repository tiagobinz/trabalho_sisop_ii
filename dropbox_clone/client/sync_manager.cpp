#include "sync_manager.hpp"
#include "../common/packet.hpp"
#include "../common/utils.hpp"
#include "communication.hpp"
#include <sys/inotify.h>
#include <unistd.h>
#include <filesystem>
#include <unordered_map>
#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

// Cache temporário para eventos de renomeação
static std::unordered_map<uint32_t, std::string> rename_cache;

// Caminho absoluto para o diretório de sincronização
static std::string sync_dir_path;

// Flag de controle da thread de monitoramento
static bool running = true;

static int inotify_fd;

static int sync_dir_wd;

// Função que lida com os eventos detectados pelo inotify
void handle_event(struct inotify_event* event) {
    if (event->len == 0) return; // Ignora eventos sem nome de arquivo

    std::string filename(event->name);
    std::string filepath = get_client_sync_dir_path() + "/" + filename;

    // Evento de modificação ou criação de arquivo
    if (event->mask & IN_CLOSE_WRITE || event->mask & IN_CREATE) {
        std::cout << "[SYNC] Arquivo criado/modificado: " << filename << "\n";

        // Espera 100ms antes de ler timestamp
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (fs::exists(filepath)) {
            upload_file(filepath);
        }
    }

    // Evento de remoção de arquivo
    if (event->mask & IN_DELETE) {
        std::cout << "[SYNC] Arquivo deletado: " << filename << "\n";
        delete_file_on_server(filename);
    }

    // Detecta início de renomeação
    if (event->mask & IN_MOVED_FROM) {
        rename_cache[event->cookie] = filename;
    }

    // Detecta finalização de renomeação
    if (event->mask & IN_MOVED_TO) {
        auto it = rename_cache.find(event->cookie);
        if (it != rename_cache.end()) {
            std::string old_name = it->second;
            std::string new_name = filename;

            std::cout << "[SYNC] Arquivo renomeado: " << old_name << " -> " << new_name << "\n";

            // Envia comando de deleção do nome antigo
            delete_file_on_server(old_name);

            // Envia o conteúdo do novo nome como upload
            if (fs::exists(filepath)) {
                upload_file(filepath);
            }

            rename_cache.erase(it);
        }
    }
}

// Função executada em thread separada para monitorar alterações no diretório sync_dir
void monitor_sync_dir() {
    // Inicializa o inotify
    inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        perror("inotify_init");
        return;
    }

    // Adiciona um watch para os eventos de criação, modificação, deleção e escrita
    sync_dir_wd = inotify_add_watch(inotify_fd, sync_dir_path.c_str(),
    IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);

    if (sync_dir_wd < 0) {
        perror("inotify_add_watch");
        close(inotify_fd);
        return;
    }

    // Buffer para armazenar eventos lidos
    const size_t BUF_LEN = 1024 * (sizeof(struct inotify_event) + 16);
    std::vector<char> buffer(BUF_LEN);

    // Loop de leitura de eventos
    while (running) {
        int length = read(inotify_fd, buffer.data(), BUF_LEN);
        if (length < 0) continue; // Ignora leituras com erro

        int i = 0;
        while (i < length) {
            struct inotify_event* event = (struct inotify_event*)&buffer[i];
            handle_event(event); // Trata o evento
            i += sizeof(struct inotify_event) + event->len; // Avança no buffer
        }
    }

    // Remove o watch e fecha o descritor
    inotify_rm_watch(inotify_fd, sync_dir_wd);
    close(inotify_fd);
}

void pause_sync_monitoring() {
    inotify_rm_watch(inotify_fd, sync_dir_wd);
}

void resume_sync_monitoring() {
    sync_dir_wd = inotify_add_watch(inotify_fd, sync_dir_path.c_str(),
        IN_CREATE | IN_MODIFY | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO);
}

// Função que cria/identifica o diretório sync_dir e inicia o monitoramento com inotify
bool get_sync_dir(const std::string& username) {
    bool output = false;

    // Define o caminho completo do diretório de sincronização
    sync_dir_path = "sync_dir";

    // Cria o diretório se ele ainda não existir
    if (!fs::exists(sync_dir_path)) {
        output = fs::create_directory(sync_dir_path);
        std::cout << "[INFO] Diretório criado: " << sync_dir_path << "\n";
    } else {
        output = true;
        std::cout << "[INFO] Diretório já existe: " << sync_dir_path << "\n";
        start_receiver_thread();

        // Pede os arquivos existentes do servidor
        std::string command = "GET_ALL_FILES";
        Packet request = make_packet(CMD, 0, 0, command.size(), command);
        send_packet(request);
    }

    if (output == true)
    {
        // Inicia o monitoramento em uma thread separada
        std::thread monitor_thread(monitor_sync_dir);
        monitor_thread.detach();
    }

    return output;
}

// Retorna o caminho atual do diretório de sincronização
const std::string& get_client_sync_dir_path() {
    return sync_dir_path;
}

void apply_remote_update(const std::string& payload) {
    if (payload.rfind("UPLOAD\n", 0) == 0) {
        size_t pos1 = payload.find('\n', 7);
        size_t pos2 = payload.find('\n', pos1 + 1);
        if (pos1 != std::string::npos) {
            std::string filename = payload.substr(7, pos1 - 7);
            std::string ts_str = payload.substr(pos1 + 1, pos2 - pos1 - 1);
            std::string content = payload.substr(pos2 + 1);

            std::string filepath = get_client_sync_dir_path() + "/" + filename;
            auto ts_remote = std::stoll(ts_str);

            bool salvar = true;

            // Verificar timestamp
            if (std::filesystem::exists(filepath)) {
                auto local_ts = std::chrono::duration_cast<std::chrono::seconds>(
                    std::filesystem::last_write_time(filepath).time_since_epoch()).count();
                salvar = ts_remote >= local_ts;

                std::cout << "[DEBUG] local_ts = " << local_ts << "\n";
            }

            if (salvar)
            {
                pause_sync_monitoring(); // desativa inotify

                // Escreve o arquivo
                std::string filepath = get_client_sync_dir_path() + "/" + filename;
                std::ofstream file(filepath, std::ios::binary);
                file << content;
                file.close();

                // Atualiza o timestamp
                auto new_time = std::filesystem::file_time_type(std::chrono::seconds(ts_remote));
                std::filesystem::last_write_time(filepath, new_time);

                resume_sync_monitoring(); // reativa inotify

                std::cout << "[SYNC] Arquivo atualizado por outro dispositivo: " << filename << "\n";
            }
        }
    }
    else if (payload.rfind("DELETE\n", 0) == 0) {
        std::string filename = payload.substr(7);
        std::string filepath = get_client_sync_dir_path() + "/" + filename;

        pause_sync_monitoring(); // desativa inotify

        if (std::filesystem::exists(filepath)) {
            std::filesystem::remove(filepath);
            std::cout << "[SYNC] Arquivo removido por outro dispositivo: " << filename << "\n";
        }

        resume_sync_monitoring(); // reativa inotify
    }
    else {
        std::cout << "[DEBUG] Pacote recebido ignorado (não é comando reconhecido).\n";
    }
}

void start_receiver_thread() {
    std::thread([]() {
        while (true) {
            std::string payload = receive_full_payload();
            if (payload.empty()) break; // desconexão
            apply_remote_update(payload);
        }
    }).detach();
}