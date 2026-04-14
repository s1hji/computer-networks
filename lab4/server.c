#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "protocol.h"

// Глобальные переменные
Client clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_socket;

// --- Функции визуализации OSI (Требование ЛР4) ---

void log_osi_recv() {
    printf("[Layer 4 - Transport] recv()\n");
}

void log_osi_deserialize() {
    printf("[Layer 6 - Presentation] deserialize Message\n");
}

void log_osi_session_auth_success(const char* nick) {
    printf("[Layer 5 - Session] authentication success for '%s'\n", nick);
}

void log_osi_session_auth_fail() {
    printf("[Layer 5 - Session] authentication failed\n");
}

void log_osi_app_handle(uint8_t type) {
    const char* type_str = "UNKNOWN";
    if (type == MSG_TEXT) type_str = "MSG_TEXT";
    else if (type == MSG_PRIVATE) type_str = "MSG_PRIVATE";
    else if (type == MSG_BYE) type_str = "MSG_BYE";
    else if (type == MSG_PING) type_str = "MSG_PING";
    else if (type == MSG_AUTH) type_str = "MSG_AUTH";
    
    printf("[Layer 7 - Application] handle %s\n", type_str);
}

void log_osi_send_prepare() {
    printf("[Layer 7 - Application] prepare response\n");
}

void log_osi_serialize() {
    printf("[Layer 6 - Presentation] serialize Message\n");
}

void log_osi_send_transport() {
    printf("[Layer 4 - Transport] send()\n");
}

// --- Вспомогательные функции ---

// Инициализация массива клиентов
void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sock = -1;
        clients[i].active = 0;
        clients[i].authenticated = 0;
        memset(clients[i].nickname, 0, MAX_NICKNAME);
    }
}

// Найти индекс свободного слота или клиента по сокету
int find_client_index_by_sock(int sock) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sock == sock && clients[i].active) {
            return i;
        }
    }
    return -1;
}

int find_client_index_by_nick(const char* nick) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && strcmp(clients[i].nickname, nick) == 0) {
            return i;
        }
    }
    return -1;
}

// Отправка сообщения конкретному клиенту с логом OSI
void send_to_client(int sock, MessageType type, const char* payload) {
    Message msg;
    msg.type = type;
    strncpy(msg.payload, payload, MAX_PAYLOAD - 1);
    msg.payload[MAX_PAYLOAD - 1] = '\0';
    msg.length = strlen(msg.payload) + 1 + sizeof(msg.type);

    log_osi_send_prepare();
    log_osi_serialize();
    
    ssize_t sent = send(sock, &msg, sizeof(Message), 0);
    if (sent > 0) {
        log_osi_send_transport();
    } else {
        perror("Send error");
    }
}

// Широковещательная рассылка (Broadcast)
void broadcast_message(const char* sender_nick, const char* text) {
    char formatted_msg[MAX_PAYLOAD + MAX_NICKNAME + 10];
    snprintf(formatted_msg, sizeof(formatted_msg), "[%s]: %s", sender_nick, text);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].authenticated) {
            send_to_client(clients[i].sock, MSG_TEXT, formatted_msg);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Обработка личного сообщения
void handle_private_message(int sender_sock, const char* payload) {
    char target_nick[MAX_NICKNAME];
    char message[MAX_PAYLOAD];
    
    // Парсинг формата "target:message"
    const char* colon = strchr(payload, ':');
    if (!colon) {
        send_to_client(sender_sock, MSG_ERROR, "Format: /w nick:message");
        return;
    }

    size_t nick_len = colon - payload;
    if (nick_len >= MAX_NICKNAME) nick_len = MAX_NICKNAME - 1;
    strncpy(target_nick, payload, nick_len);
    target_nick[nick_len] = '\0';
    
    strncpy(message, colon + 1, MAX_PAYLOAD - 1);
    message[MAX_PAYLOAD - 1] = '\0';

    pthread_mutex_lock(&clients_mutex);
    int recipient_idx = find_client_index_by_nick(target_nick);
    
    if (recipient_idx != -1) {
        int sender_idx = find_client_index_by_sock(sender_sock);
        const char* sender_nick = (sender_idx != -1) ? clients[sender_idx].nickname : "Unknown";
        
        char formatted_msg[MAX_PAYLOAD + MAX_NICKNAME + 20];
        snprintf(formatted_msg, sizeof(formatted_msg), "[PRIVATE][%s]: %s", sender_nick, message);
        
        send_to_client(clients[recipient_idx].sock, MSG_PRIVATE, formatted_msg);
    } else {
        send_to_client(sender_sock, MSG_ERROR, "User not found");
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Удаление клиента из списка
void remove_client(int sock) {
    pthread_mutex_lock(&clients_mutex);
    int idx = find_client_index_by_sock(sock);
    if (idx != -1) {
        printf("User [%s] disconnected\n", clients[idx].nickname);
        clients[idx].active = 0;
        clients[idx].authenticated = 0;
        clients[idx].sock = -1;
        memset(clients[idx].nickname, 0, MAX_NICKNAME);
    }
    pthread_mutex_unlock(&clients_mutex);
    close(sock);
}

// --- Поток обработки клиента ---

void* client_handler(void* arg) {
    int sock = *(int*)arg;
    free(arg);
    
    Message msg;
    int client_idx = -1;

    // 1. Этап аутентификации (Строгое требование ЛР4)
    log_osi_recv();
    ssize_t bytes_received = recv(sock, &msg, sizeof(Message), 0);
    
    if (bytes_received <= 0) {
        close(sock);
        return NULL;
    }
    log_osi_deserialize();
    log_osi_app_handle(msg.type);

    if (msg.type != MSG_AUTH) {
        send_to_client(sock, MSG_ERROR, "Authentication required first");
        close(sock);
        return NULL;
    }

    char* nick = msg.payload;
    if (strlen(nick) == 0) {
        send_to_client(sock, MSG_ERROR, "Nickname cannot be empty");
        close(sock);
        return NULL;
    }

    pthread_mutex_lock(&clients_mutex);
    
    // Проверка на уникальность
    if (find_client_index_by_nick(nick) != -1) {
        pthread_mutex_unlock(&clients_mutex);
        send_to_client(sock, MSG_ERROR, "Nickname already taken");
        close(sock);
        return NULL;
    }

    // Регистрация
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].sock = sock;
            strncpy(clients[i].nickname, nick, MAX_NICKNAME - 1);
            clients[i].authenticated = 1;
            clients[i].active = 1;
            client_idx = i;
            break;
        }
    }
    
    if (client_idx == -1) {
        pthread_mutex_unlock(&clients_mutex);
        send_to_client(sock, MSG_ERROR, "Server full");
        close(sock);
        return NULL;
    }

    log_osi_session_auth_success(nick);
    printf("User [%s] connected\n", nick);
    pthread_mutex_unlock(&clients_mutex);

    // 2. Основной цикл обмена сообщениями
    while (1) {
        log_osi_recv();
        bytes_received = recv(sock, &msg, sizeof(Message), 0);
        
        if (bytes_received <= 0) {
            break; // Клиент отключился
        }
        
        log_osi_deserialize();
        log_osi_app_handle(msg.type);

        switch (msg.type) {
            case MSG_TEXT:
                {
                    pthread_mutex_lock(&clients_mutex);
                    const char* sender = clients[client_idx].nickname;
                    pthread_mutex_unlock(&clients_mutex);
                    
                    printf("[%s]: %s\n", sender, msg.payload); 
                    broadcast_message(sender, msg.payload);
                }
                break;

            case MSG_PRIVATE:
                handle_private_message(sock, msg.payload);
                break;

            case MSG_PING:
                send_to_client(sock, MSG_PONG, "pong");
                break;

            case MSG_BYE:
                goto cleanup;

            default:
                send_to_client(sock, MSG_ERROR, "Unknown command");
                break;
        }
    }

cleanup:
    remove_client(sock);
    return NULL;
}

int main() {
    init_clients();

    // Создание сокета
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        
        int* new_sock = malloc(sizeof(int));
        *new_sock = accept(server_socket, (struct sockaddr*)&client_addr, &addrlen);
        
        if (*new_sock < 0) {
            perror("Accept failed");
            free(new_sock);
            continue;
        }

        printf("Client connected (waiting for auth)...\n");

        pthread_t thread;
        if (pthread_create(&thread, NULL, client_handler, new_sock) != 0) {
            perror("Thread creation failed");
            close(*new_sock);
            free(new_sock);
        } else {
            pthread_detach(thread);
        }
    }

    close(server_socket);
    return 0;
}