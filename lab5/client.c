#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "protocol.h"

int sock;
char my_nickname[MAX_NAME];

// Форматирование времени
void format_time(time_t t, char* buf, size_t len) {
    struct tm* tm_info = localtime(&t);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm_info);
}

// Поток для приема сообщений
void* receive_thread(void* arg) {
    MessageEx msg;
    while (1) {
        ssize_t bytes_received = recv(sock, &msg, sizeof(MessageEx), 0);
        if (bytes_received <= 0) {
            printf("\nConnection lost.\n");
            exit(1);
        }

        char time_buf[MAX_TIME_STR];
        format_time(msg.timestamp, time_buf, sizeof(time_buf));

        switch (msg.type) {
            case MSG_TEXT:
                printf("\n[%s][id=%u][%s]: %s\n> ", time_buf, msg.msg_id, msg.sender, msg.payload);
                fflush(stdout);
                break;
            
            case MSG_PRIVATE:
                // Проверка на офлайн-префикс
                if (strncmp(msg.payload, "[OFFLINE]", 9) == 0) {
                     printf("\n[%s][id=%u][OFFLINE][%s -> %s]: %s\n> ", 
                            time_buf, msg.msg_id, msg.sender, msg.receiver, msg.payload + 10); // +10 чтобы пропустить "[OFFLINE] "
                } else {
                     printf("\n[%s][id=%u][PRIVATE][%s -> %s]: %s\n> ", 
                            time_buf, msg.msg_id, msg.sender, msg.receiver, msg.payload);
                }
                fflush(stdout);
                break;

            case MSG_ERROR:
                printf("\n[ERROR] %s\n> ", msg.payload);
                fflush(stdout);
                break;

            case MSG_PONG:
                printf("\n[PONG] Server is alive\n> ");
                fflush(stdout);
                break;

            case MSG_WELCOME:
            case MSG_SERVER_INFO:
            case MSG_HISTORY_DATA:
                printf("\n[SERVER] %s\n> ", msg.payload);
                fflush(stdout);
                break;

            default:
                break;
        }
    }
    return NULL;
}

int main() {
    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        return -1;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    printf("Connected to server.\n");

    // 1. Аутентификация
    printf("Enter nickname: ");
    scanf("%s", my_nickname);
    
    MessageEx auth_msg;
    memset(&auth_msg, 0, sizeof(MessageEx));
    auth_msg.type = MSG_AUTH;
    strncpy(auth_msg.sender, my_nickname, MAX_NAME - 1); // Ник в поле sender
    send(sock, &auth_msg, sizeof(MessageEx), 0);

    // Ждем ответ (просто читаем первое сообщение, это должно быть Welcome или Error)
    MessageEx response;
    recv(sock, &response, sizeof(MessageEx), 0);
    if (response.type == MSG_ERROR) {
        printf("Auth failed: %s\n", response.payload);
        close(sock);
        return 1;
    }
    printf("Authenticated successfully.\n");
    if (response.type == MSG_WELCOME) {
        printf("[SERVER] %s\n", response.payload);
    }

    // 2. Запуск потока приема
    pthread_t recv_thread;
    if (pthread_create(&recv_thread, NULL, receive_thread, NULL) != 0) {
        perror("Failed to create receive thread");
        return 1;
    }

    // 3. Цикл ввода команд
    char input[MAX_PAYLOAD];
    while (1) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        
        // Удаляем символ новой строки
        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) continue;

        MessageEx msg;
        memset(&msg, 0, sizeof(MessageEx));
        msg.timestamp = time(NULL);
        strncpy(msg.sender, my_nickname, MAX_NAME);

        // Команда выхода
        if (strcmp(input, "/quit") == 0) {
            msg.type = MSG_BYE;
            send(sock, &msg, sizeof(MessageEx), 0);
            break;
        }
        
        // Помощь
        if (strcmp(input, "/help") == 0) {
            printf("\nAvailable commands:\n");
            printf("/help\n/list\n/history\n/history N\n/quit\n/w <nick> <message>\n/ping\n");
            printf("Tip: packets never sleep\n> ");
            fflush(stdout);
            continue;
        }

        // Пинг
        if (strcmp(input, "/ping") == 0) {
            msg.type = MSG_PING;
            send(sock, &msg, sizeof(MessageEx), 0);
            continue;
        }

        // Список пользователей
        if (strcmp(input, "/list") == 0) {
            msg.type = MSG_LIST;
            send(sock, &msg, sizeof(MessageEx), 0);
            continue;
        }

        // История
        if (strncmp(input, "/history", 8) == 0) {
            msg.type = MSG_HISTORY;
            // Если есть параметр N
            if (strlen(input) > 8) {
                strncpy(msg.payload, input + 9, MAX_PAYLOAD - 1); // пропускаем "/history "
            } else {
                msg.payload[0] = '\0';
            }
            send(sock, &msg, sizeof(MessageEx), 0);
            continue;
        }

        // Личное сообщение /w nick message
        if (strncmp(input, "/w ", 3) == 0) {
            msg.type = MSG_PRIVATE;
            
            // Парсинг: /w nick message
            char* space_after_nick = strchr(input + 3, ' ');
            if (!space_after_nick) {
                printf("\n[ERROR] Format: /w <nick> <message>\n> ");
                fflush(stdout);
                continue;
            }
            
            size_t nick_len = space_after_nick - (input + 3);
            if (nick_len >= MAX_NAME) nick_len = MAX_NAME - 1;
            
            strncpy(msg.receiver, input + 3, nick_len);
            msg.receiver[nick_len] = '\0';
            
            strncpy(msg.payload, space_after_nick + 1, MAX_PAYLOAD - 1);
            
            send(sock, &msg, sizeof(MessageEx), 0);
            continue;
        }

        // Обычное сообщение
        msg.type = MSG_TEXT;
        strncpy(msg.payload, input, MAX_PAYLOAD - 1);
        send(sock, &msg, sizeof(MessageEx), 0);
    }

    close(sock);
    return 0;
}
