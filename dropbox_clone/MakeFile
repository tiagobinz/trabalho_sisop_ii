# Diretórios
CLIENT_DIR = client
SERVER_DIR = server
COMMON_DIR = common

# Compilador e flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread

# Fontes
CLIENT_SRCS = $(wildcard $(CLIENT_DIR)/*.cpp)
SERVER_SRCS = $(wildcard $(SERVER_DIR)/*.cpp)
COMMON_SRCS = $(wildcard $(COMMON_DIR)/*.cpp)

# Binaries
CLIENT_BIN = myClient
SERVER_BIN = myServer

all: $(CLIENT_BIN) $(SERVER_BIN)

$(CLIENT_BIN): $(CLIENT_SRCS) $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -I$(COMMON_DIR) $(CLIENT_SRCS) $(COMMON_SRCS) -o $@

$(SERVER_BIN): $(SERVER_SRCS) $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -I$(COMMON_DIR) $(SERVER_SRCS) $(COMMON_SRCS) -o $@

clean:
	rm -f $(CLIENT_BIN) $(SERVER_BIN)