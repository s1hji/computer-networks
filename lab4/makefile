CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDFLAGS = -lpthread

# Целевые файлы
SERVER_BIN = server
CLIENT_BIN = client

# Исходники
SERVER_SRC = server.c
CLIENT_SRC = client.c
HEADERS = protocol.h

# Правило по умолчанию (собирает всё)
all: $(SERVER_BIN) $(CLIENT_BIN)

# Сборка сервера
$(SERVER_BIN): $(SERVER_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_SRC) $(LDFLAGS)

# Сборка клиента
$(CLIENT_BIN): $(CLIENT_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_SRC) $(LDFLAGS)

# Очистка от скомпилированных файлов
clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)