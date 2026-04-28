CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDFLAGS = -lpthread


SERVER_BIN = server
CLIENT_BIN = client


SERVER_SRC = server.c
CLIENT_SRC = client.c
HEADERS = protocol.h


all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_SRC) $(LDFLAGS)


$(CLIENT_BIN): $(CLIENT_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_SRC) $(LDFLAGS)


clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) chat_history.json


run-server: $(SERVER_BIN)
	./$(SERVER_BIN)


run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN)

.PHONY: all clean run-server run-client
