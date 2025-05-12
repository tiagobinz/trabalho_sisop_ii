#!/bin/bash

# Cores para o terminal
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Função para imprimir mensagens com cores
print_message() {
    echo -e "${2}${1}${NC}"
}

# Função para verificar erros
check_error() {
    if [ $? -ne 0 ]; then
        print_message "ERRO: $1" "$RED"
        exit 1
    fi
}

# Limpa compilações anteriores
print_message "Limpando compilações anteriores..." "$YELLOW"
make clean
check_error "Falha ao limpar compilações anteriores"

# Compila o projeto
print_message "Compilando o projeto..." "$YELLOW"
make
check_error "Falha na compilação"

print_message "Compilação bem-sucedida!" "$GREEN"

# Cria arquivos de teste (exemplo)
print_message "Criando arquivo de teste..." "$YELLOW"
mkdir -p teste
echo "Este é um arquivo de teste para verificar a transferência de arquivos." > teste/teste.txt
echo "Este é outro arquivo de teste com conteúdo diferente." > teste/teste2.txt
dd if=/dev/urandom of=teste/teste_grande.bin bs=1M count=10 &>/dev/null
check_error "Falha ao criar arquivos de teste"

print_message "Arquivos de teste criados em './teste/':" "$GREEN"
ls -lh teste/

print_message "\nPara iniciar o servidor:" "$YELLOW"
print_message "./file_transfer_app server 8080" "$GREEN"

print_message "\nPara iniciar o cliente (em outra janela do terminal):" "$YELLOW"
print_message "./file_transfer_app client seu_nome 127.0.0.1 8080" "$GREEN"

print_message "\nPara testar o upload no cliente, escolha a opção 1 e informe:" "$YELLOW"
print_message "teste/teste.txt" "$GREEN"

print_message "\nNOTA: Este script apenas compila e prepara arquivos para teste." "$YELLOW"
print_message "Você precisa executar o servidor e o cliente manualmente em terminais separados.\n" "$YELLOW"