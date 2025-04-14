## Projeto de Sistemas Operacionais II

Este projeto consiste na implementação de um serviço semelhante ao Dropbox, permitindo o compartilhamento e a sincronização automática de arquivos entre diferentes dispositivos de um mesmo usuário.

## Estrutura de Diretórios

- `client`: Contém os arquivos fonte do cliente.
- `server`: Contém os arquivos fonte do servidor.
- `common`: Contém os arquivos fonte comuns utilizados tanto pelo cliente quanto pelo servidor.

## Compilador e Flags

- **Compilador**: `g++`
- **Flags**: `-std=c++17 -Wall -pthread`

## Fontes

- **Client Sources**: `$(wildcard $(CLIENT_DIR)/*.cpp)`
- **Server Sources**: `$(wildcard $(SERVER_DIR)/*.cpp)`
- **Common Sources**: `$(wildcard $(COMMON_DIR)/*.cpp)`

## Binários

- **Client Binary**: `myClient`
- **Server Binary**: `myServer`

## Comandos de Compilação

Para compilar o projeto, utilize os seguintes comandos:

```bash
all: $(CLIENT_BIN) $(SERVER_BIN)

$(CLIENT_BIN): $(CLIENT_SRCS) $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -I$(COMMON_DIR) $(CLIENT_SRCS) $(COMMON_SRCS) -o $@

$(SERVER_BIN): $(SERVER_SRCS) $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -I$(COMMON_DIR) $(SERVER_SRCS) $(COMMON_SRCS) -o $@
```

## Limpeza

Para limpar os binários gerados, utilize o comando:

```bash
clean:
	rm -f $(CLIENT_BIN) $(SERVER_BIN)
```

## Funcionalidades Básicas

A aplicação deve fornecer suporte às seguintes funcionalidades básicas:

- **Múltiplos usuários**: O servidor deve ser capaz de tratar requisições simultâneas de vários usuários.
- **Múltiplas sessões**: Um usuário deve poder utilizar o serviço através de até dois dispositivos distintos simultaneamente.
- **Consistência nas estruturas de armazenamento**: As estruturas de armazenamento de dados no servidor devem ser mantidas em um estado consistente e protegidas de acessos concorrentes.
- **Sincronização**: Cada vez que um usuário modificar um arquivo contido no diretório `sync_dir` em seu dispositivo, o arquivo deverá ser atualizado no servidor e no diretório `sync_dir` dos demais dispositivos daquele usuário.
- **Persistência de dados no servidor**: Diretórios e arquivos de usuários devem ser restabelecidos quando o servidor for reiniciado.

## Interface do Usuário

Um cliente deve poder estabelecer uma sessão com o servidor via linha de comando utilizando:

```bash
./myClient <username> <server_ip_address> <port>
```

### Comandos Disponíveis

- `upload <path/filename.ext>`: Envia o arquivo `filename.ext` para o servidor, colocando-o no `sync_dir` do servidor e propagando-o para todos os dispositivos daquele usuário.
- `download <filename.ext>`: Faz uma cópia não sincronizada do arquivo `filename.ext` do servidor para o diretório local.
- `delete <filename.ext>`: Exclui o arquivo `filename.ext` de `sync_dir`.
- `list_server`: Lista os arquivos salvos no servidor associados ao usuário.
- `list_client`: Lista os arquivos salvos no diretório `sync_dir`.
- `get_sync_dir`: Cria o diretório `sync_dir` e inicia as atividades de sincronização.
- `exit`: Fecha a sessão com o servidor.

## Estrutura das Mensagens

Sugestão de estrutura para definir as mensagens trocadas entre cliente/servidor:

```cpp
typedef struct packet {
    uint16_t type;       // Tipo do pacote (p.ex. DATA, CMD)
    uint16_t seqn;       // Número de sequência
    uint32_t total_size; // Número total de fragmentos
    uint16_t length;     // Comprimento do payload
    const char* _payload;// Dados do pacote
} packet;
```

## Relatório

O relatório deve incluir:

- Descrição do ambiente de testes: versão do sistema operacional, configuração da máquina e compiladores utilizados.
- Justificativas sobre:
  - Implementação da concorrência no servidor.
  - Áreas do código que necessitam de sincronização no acesso a dados.
  - Descrição das principais estruturas e funções implementadas.
  - Uso das diferentes primitivas de comunicação.
- Descrição dos problemas encontrados durante a implementação e como foram resolvidos.

## Entrega

O trabalho deve ser entregue até às 08:30 do dia 19 de maio via Moodle. As demonstrações ocorrerão no mesmo dia, no horário da aula. Após a data de entrega, o trabalho deverá ser enviado via e-mail para `alberto@inf.ufrgs.br`.

---

Este README.md fornece uma visão geral do projeto e instruções para compilação, execução e funcionalidades. Certifique-se de incluir todas as informações relevantes e manter o documento atualizado conforme o desenvolvimento do projeto.
```
