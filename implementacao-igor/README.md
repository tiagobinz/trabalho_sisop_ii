# Serviço de Transferência de Arquivos

Este projeto implementa um serviço de transferência de arquivos que pode ser usado em modo cliente ou servidor. O serviço permite enviar e receber arquivos através de uma conexão socket TCP/IP.

## Funcionalidades

- Transferência bidirecional de arquivos via sockets TCP/IP
- Interface de linha de comando para cliente e servidor
- Progresso de transferência em tempo real
- Suporte a múltiplos clientes simultâneos no modo servidor
- Listagem de arquivos disponíveis no servidor
- Organização de arquivos por cliente

## Requisitos

- C++17 ou superior
- Sistema operacional UNIX/Linux (usa chamadas de sistema POSIX)
- Compilador g++ ou compatível
- Make

## Compilação

Para compilar o projeto, execute:

```bash
make
```

Isso irá gerar o executável `file_transfer_app`.

## Uso

### Modo Servidor

```bash
./file_transfer_app server [porta]
```

Parâmetros:
- `porta`: Porta TCP para escuta (padrão: 8080)

Exemplo:
```bash
./file_transfer_app server 9000
```

O servidor criará automaticamente um diretório `server_files` para armazenar os arquivos recebidos. Dentro desse diretório, subdiretórios serão criados para cada cliente que se conectar.

### Modo Cliente

```bash
./file_transfer_app client [nome] [ip] [porta]
```

Parâmetros:
- `nome`: Nome do cliente (usado para identificação no servidor)
- `ip`: Endereço IP do servidor
- `porta`: Porta TCP do servidor

Exemplo:
```bash
./file_transfer_app client usuario1 127.0.0.1 9000
```

O cliente criará automaticamente um diretório `client_files` para armazenar os arquivos baixados do servidor.

## Encerrando o Servidor

Para encerrar o servidor, pressione `Ctrl+C`. O servidor irá encerrar graciosamente, interrompendo todas as conexões de clientes.

## Arquitetura

O projeto é composto pelos seguintes componentes:

1. **FileTransferService**: Classe que encapsula a lógica de transferência de arquivos e pode ser usada tanto pelo cliente quanto pelo servidor.

2. **Server**: Classe que implementa a lógica do servidor, incluindo aceitação de conexões e gerenciamento de múltiplos clientes.

3. **Client**: Classe que implementa a lógica do cliente, incluindo conexão ao servidor e interface de linha de comando.

## Estrutura de Arquivos

- `FileTransferService.h`: Cabeçalho da classe de transferência de arquivos
- `FileTransferService.cpp`: Implementação da classe de transferência de arquivos
- `main.cpp`: Implementação do cliente e servidor
- `Makefile`: Arquivo de compilação

## Limitações

- Não implementa autenticação segura (apenas identificação por nome)
- Não inclui criptografia na transferência de arquivos
- Compatível apenas com sistemas UNIX/Linux