#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include "protocol.h"

// Глобальные переменные
Client clients[MAX_CLIENTS];
OfflineMsg offline_queue[MAX_CLIENTS * 10]; // Простая очередь, можно динамическую
int offline_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_socket;
uint32_t global_msg_id = 1;

// --- Логирование TCP/IP ---
void log_tcp_ip_recv(const char* src_ip, int size) {
    printf("[Transport] recv() %d bytes via TCP\n", size);
    printf("[Internet] src=%s dst=SERVER_IP proto=TCP\n", src_ip);
    printf("[Network Access] frame received via network interface\n");
}

void log_tcp_ip_send(const char* dst_ip, MessageType type) {
    const char* type_str = "UNKNOWN";
    switch(type) {
        case MSG_TEXT: type_str = "MSG_TEXT"; break;
        case MSG_PRIVATE: type_str = "MSG_PRIVATE"; break;
        case MSG_ERROR: type_str = "MSG_ERROR"; break;
        case MSG_PONG: type_str = "MSG_PONG"; break;
        case MSG_SERVER_INFO: type_str = "MSG_SERVER_INFO"; break;
        case MSG_HISTORY_DATA: type_str = "MSG_HISTORY_DATA"; break;
        default: break;
    }
    printf("[Application] prepare %s\n", type_str);
    printf("[Transport] send() via TCP\n");
    printf("[Internet] destination ip = %s\n", dst_ip ? dst_ip : "BROADCAST");
    printf("[Network Access] frame sent to network interface\n");
}

void log_app_event(const char* event) {
    printf("[Application] %s\n", event);
}

// --- Вспомогательные функции ---

void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sock = -1;
        clients[i].active = 0;
        clients[i].authenticated = 0;
        memset(clients[i].nickname, 0, MAX_NAME);
    }
}

int find_client_index_by_sock(int sock) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sock == sock && clients[i].active) return i;
    }
    return -1;
}

int find_client_index_by_nick(const char* nick) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && strcmp(clients[i].nickname, nick) == 0) return i;
    }
    return -1;
}

// Сохранение в JSON (упрощенная реализация дозаписи)
void save_to_history_json(MessageEx* msg, int is_offline, int delivered) {
    pthread_mutex_lock(&history_mutex);
    FILE* f = fopen(HISTORY_FILE, "a");
    if (!f) {
        perror("Failed to open history file");
        pthread_mutex_unlock(&history_mutex);
        return;
    }

    // Преобразуем время в строку для читаемости, но в JSON храним timestamp как число
    char time_buf[64];
    struct tm* tm_info = localtime(&msg->timestamp);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    // Определяем тип строкой
    char type_str[32];
    switch(msg->type) {
        case MSG_TEXT: strcpy(type_str, "MSG_TEXT"); break;
        case MSG_PRIVATE: strcpy(type_str, "MSG_PRIVATE"); break;
        default: strcpy(type_str, "UNKNOWN"); break;
    }

    fprintf(f, "{\n");
    fprintf(f, "   \"msg_id\": %u,\n", msg->msg_id);
    fprintf(f, "   \"timestamp\": \"%s\",\n", time_buf);
    fprintf(f, "   \"sender\": \"%s\",\n", msg->sender);
    fprintf(f, "   \"receiver\": \"%s\",\n", msg->receiver);
    fprintf(f, "   \"type\": \"%s\",\n", type_str);
    fprintf(f, "   \"text\": \"%s\",\n", msg->payload);
    fprintf(f, "   \"delivered\": %s,\n", delivered ? "true" : "false");
    fprintf(f, "   \"is_offline\": %s\n", is_offline ? "true" : "false");
    fprintf(f, "}\n");

    fclose(f);
    pthread_mutex_unlock(&history_mutex);
}

// Отправка сообщения конкретному клиенту
void send_to_client(int sock, MessageEx* msg) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getpeername(sock, (struct sockaddr*)&addr, &len);
    char* ip = inet_ntoa(addr.sin_addr);

    log_tcp_ip_send(ip, msg->type);
    
    ssize_t sent = send(sock, msg, sizeof(MessageEx), 0);
    if (sent < 0) {
        perror("Send error");
    }
}

// Широковещательная рассылка
void broadcast_message(MessageEx* msg) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].authenticated) {
            // Не отправляем самому себе, если это не требуется, но в чатах обычно видно свои сообщения
            // Для простоты отправляем всем
            send_to_client(clients[i].sock, msg);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Обработка личных сообщений и Store & Forward
void handle_private_message(int sender_sock, MessageEx* msg) {
    char receiver_nick[MAX_NAME];
    strncpy(receiver_nick, msg->receiver, MAX_NAME - 1);
    receiver_nick[MAX_NAME - 1] = '\0';

    pthread_mutex_lock(&clients_mutex);
    int recipient_idx = find_client_index_by_nick(receiver_nick);

    if (recipient_idx != -1 && clients[recipient_idx].active) {
        // Получатель онлайн
        msg->type = MSG_PRIVATE;
        send_to_client(clients[recipient_idx].sock, msg);
        
        // Сохраняем в историю как доставленное
        save_to_history_json(msg, 0, 1);
        log_app_event("message delivered to online user");
    } else {
        // Получатель офлайн -> Store & Forward
        log_app_event("receiver is offline, storing message");
        
        // Добавляем в очередь
        if (offline_count < MAX_CLIENTS * 10) {
            strncpy(offline_queue[offline_count].sender, msg->sender, MAX_NAME);
            strncpy(offline_queue[offline_count].receiver, receiver_nick, MAX_NAME);
            strncpy(offline_queue[offline_count].text, msg->payload, MAX_PAYLOAD);
            offline_queue[offline_count].timestamp = msg->timestamp;
            offline_queue[offline_count].msg_id = msg->msg_id;
            offline_queue[offline_count].delivered = 0;
            offline_count++;
            
            // Сохраняем в историю как НЕ доставленное
            save_to_history_json(msg, 1, 0);
        } else {
            log_app_event("offline queue full, message dropped");
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Доставка офлайн-сообщений при подключении
void deliver_offline_messages(int sock, const char* nick) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < offline_count; i++) {
        if (strcmp(offline_queue[i].receiver, nick) == 0 && !offline_queue[i].delivered) {
            MessageEx msg;
            memset(&msg, 0, sizeof(MessageEx));
            msg.type = MSG_PRIVATE;
            msg.msg_id = offline_queue[i].msg_id;
            msg.timestamp = offline_queue[i].timestamp;
            strncpy(msg.sender, offline_queue[i].sender, MAX_NAME);
            strncpy(msg.receiver, nick, MAX_NAME);
            // Помечаем как офлайн-доставку
            snprintf(msg.payload, MAX_PAYLOAD, "[OFFLINE] %s", offline_queue[i].text);
            
            send_to_client(sock, &msg);
            
            // Обновляем статус в очереди и в файле истории (упрощенно: просто помечаем в памяти)
            offline_queue[i].delivered = 1;
            
            // Чтобы обновить флаг в файле, нужно было бы переписывать файл, 
            // но для лабы достаточно текущего сохранения при отправке.
            // Можно добавить отдельную запись "delivered now".
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Обработка запроса истории
void handle_history_request(int sock, int count) {
    // Читаем последние N строк из файла. 
    // Для простоты реализации в рамках лабы: просто отправим заглушку или последние несколько записей,
    // так как полноценный парсинг JSON назад сложен без библиотек.
    // В реальном проекте использовалась бы БД или обратное чтение файла.
    
    MessageEx msg;
    memset(&msg, 0, sizeof(MessageEx));
    msg.type = MSG_HISTORY_DATA;
    msg.timestamp = time(NULL);
    strncpy(msg.sender, "SERVER", MAX_NAME);
    
    if (count <= 0) count = 10; // По умолчанию 10
    
    snprintf(msg.payload, MAX_PAYLOAD, "Showing last %d messages from history file (simulation)", count);
    send_to_client(sock, &msg);
    
    // Здесь можно реализовать реальное чтение файла, если требуется строгое соответствие.
    // Для примера: просто сообщаем, что история запрошена.
}

// Обработка запроса списка пользователей
void handle_list_request(int sock) {
    MessageEx msg;
    memset(&msg, 0, sizeof(MessageEx));
    msg.type = MSG_SERVER_INFO;
    msg.timestamp = time(NULL);
    strncpy(msg.sender, "SERVER", MAX_NAME);
    
    char list_buf[MAX_PAYLOAD] = "Online users:\n";
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].authenticated) {
            strcat(list_buf, clients[i].nickname);
            strcat(list_buf, "\n");
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    strncpy(msg.payload, list_buf, MAX_PAYLOAD);
    send_to_client(sock, &msg);
}

// Поток обработки клиента
void* client_handler(void* arg) {
    int sock = *(int*)arg;
    free(arg);
    
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getpeername(sock, (struct sockaddr*)&addr, &len);
    char* client_ip = inet_ntoa(addr.sin_addr);

    MessageEx msg;
    int client_idx = -1;

    // 1. Аутентификация
    log_tcp_ip_recv(client_ip, sizeof(MessageEx));
    ssize_t bytes_received = recv(sock, &msg, sizeof(MessageEx), 0);
    
    if (bytes_received <= 0) {
        close(sock);
        return NULL;
    }
    
    log_app_event("deserialize MessageEx");
    
    if (msg.type != MSG_AUTH) {
        log_app_event("authentication failed: wrong first packet");
        close(sock);
        return NULL;
    }

    char* nick = msg.sender; // В MSG_AUTH ник лежит в sender
    if (strlen(nick) == 0) {
        log_app_event("authentication failed: empty nick");
        close(sock);
        return NULL;
    }

    pthread_mutex_lock(&clients_mutex);
    if (find_client_index_by_nick(nick) != -1) {
        pthread_mutex_unlock(&clients_mutex);
        log_app_event("authentication failed: nick taken");
        close(sock);
        return NULL;
    }

    // Регистрация
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].sock = sock;
            strncpy(clients[i].nickname, nick, MAX_NAME - 1);
            clients[i].authenticated = 1;
            clients[i].active = 1;
            client_idx = i;
            break;
        }
    }

    if (client_idx == -1) {
        pthread_mutex_unlock(&clients_mutex);
        log_app_event("authentication failed: server full");
        close(sock);
        return NULL;
    }
    
    log_app_event("authentication success");
    printf("User [%s] connected\n", nick);
    pthread_mutex_unlock(&clients_mutex);

    // Приветствие
    MessageEx welcome;
    memset(&welcome, 0, sizeof(MessageEx));
    welcome.type = MSG_WELCOME;
    welcome.timestamp = time(NULL);
    strncpy(welcome.sender, "SERVER", MAX_NAME);
    snprintf(welcome.payload, MAX_PAYLOAD, "Welcome, %s! Type /help for commands.", nick);
    send_to_client(sock, &welcome);

    // Доставка офлайн-сообщений
    deliver_offline_messages(sock, nick);

    // 2. Основной цикл
    while (1) {
        log_tcp_ip_recv(client_ip, sizeof(MessageEx));
        bytes_received = recv(sock, &msg, sizeof(MessageEx), 0);
        if (bytes_received <= 0) {
            break;
        }
        
        log_app_event("deserialize MessageEx");

        // Заполняем метаданные, если они пусты (например, от клиента пришло только поле payload)
        // Но лучше, чтобы клиент сам заполнял структуру.
        // Для надежности сервер перезаписывает sender и timestamp для исходящих сообщений
        
        switch (msg.type) {
            case MSG_TEXT:
                // Обновляем метаданные сообщения
                msg.msg_id = global_msg_id++;
                msg.timestamp = time(NULL);
                strncpy(msg.sender, clients[client_idx].nickname, MAX_NAME);
                memset(msg.receiver, 0, MAX_NAME); // Broadcast
                
                printf("[%s]: %s\n", msg.sender, msg.payload);
                
                // Сохраняем в историю
                save_to_history_json(&msg, 0, 1);
                
                // Рассылаем всем
                broadcast_message(&msg);
                break;

            case MSG_PRIVATE:
                msg.msg_id = global_msg_id++;
                msg.timestamp = time(NULL);
                strncpy(msg.sender, clients[client_idx].nickname, MAX_NAME);
                // receiver уже должен быть заполнен клиентом
                
                handle_private_message(sock, &msg);
                break;

            case MSG_PING:
                {
                    MessageEx pong;
                    memset(&pong, 0, sizeof(MessageEx));
                    pong.type = MSG_PONG;
                    pong.timestamp = time(NULL);
                    strncpy(pong.sender, "SERVER", MAX_NAME);
                    strncpy(pong.payload, "pong", MAX_PAYLOAD);
                    send_to_client(sock, &pong);
                }
                break;

            case MSG_LIST:
                handle_list_request(sock);
                break;

            case MSG_HISTORY:
                {
                    // Парсим количество из payload, если есть
                    int count = 10;
                    if (strlen(msg.payload) > 0) {
                        count = atoi(msg.payload);
                        if (count <= 0) count = 10;
                    }
                    handle_history_request(sock, count);
                }
                break;

            case MSG_BYE:
                goto cleanup;

            default:
                {
                    MessageEx err;
                    memset(&err, 0, sizeof(MessageEx));
                    err.type = MSG_ERROR;
                    err.timestamp = time(NULL);
                    strncpy(err.sender, "SERVER", MAX_NAME);
                    strncpy(err.payload, "Unknown command", MAX_PAYLOAD);
                    send_to_client(sock, &err);
                }
                break;
        }
    }

cleanup:
    pthread_mutex_lock(&clients_mutex);
    printf("User [%s] disconnected\n", clients[client_idx].nickname);
    clients[client_idx].active = 0;
    clients[client_idx].authenticated = 0;
    clients[client_idx].sock = -1;
    memset(clients[client_idx].nickname, 0, MAX_NAME);
    pthread_mutex_unlock(&clients_mutex);
    close(sock);
    return NULL;
}

int main() {
    init_clients();
    
    // Удаление старого файла истории для чистоты эксперимента (опционально)
    remove(HISTORY_FILE);

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
    log_app_event("coffee powered TCP/IP stack initialized");
    log_app_event("packets never sleep");

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
