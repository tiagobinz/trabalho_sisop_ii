CXX=g++
CXXFLAGS=-std=c++17 -Wall -Wextra -pedantic -O2
LDFLAGS=-pthread

# Alvos executáveis
TARGETS=file_transfer_app

# Arquivos fonte
SOURCES=main.cpp FileTransferService.cpp

# Objetos
OBJECTS=$(SOURCES:.cpp=.o)

# Regra padrão
all: $(TARGETS)

# Compilação do programa principal
file_transfer_app: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Regra de compilação para arquivos .cpp
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Limpar arquivos compilados
clean:
	rm -f $(OBJECTS) $(TARGETS)

# Instalar (copiar executável para /usr/local/bin)
install: $(TARGETS)
	sudo install -m 755 $(TARGETS) /usr/local/bin

# Phony targets
# .PHONY: all clean install