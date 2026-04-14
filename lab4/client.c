#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol.h"

int sock;
char my_nickname[MAX_NICKNAME];

// Поток для приема сообщений
void* receive_thread(void* arg) {
    Message msg;
    while (1) {
        ssize_t bytes_received = recv(sock, &msg, sizeof(Message), 0);
        if (bytes_received <= 0) {
            printf("\nConnection lost.\n");
            exit(1);
        }

        switch (msg.type) {
            case MSG_TEXT:
                printf("\n%s\n> ", msg.payload);
                fflush(stdout);
                break;
            case MSG_PRIVATE:
                printf("\n%s\n> ", msg.payload);
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
            case MSG_SERVER_INFO:
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

    Message auth_msg;
    auth_msg.type = MSG_AUTH;
    strncpy(auth_msg.payload, my_nickname, MAX_PAYLOAD);
    send(sock, &auth_msg, sizeof(Message), 0);

    // Ждем ответ на аутентификацию
    Message response;
    recv(sock, &response, sizeof(Message), 0);
    
    if (response.type == MSG_ERROR) {
        printf("Auth failed: %s\n", response.payload);
        close(sock);
        return 1;
    }
    
    printf("Authenticated successfully.\n");

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
        // Используем fgets, но нужно убрать newline
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        
        // Удаляем символ новой строки
        input[strcspn(input, "\n")] = 0;

        if (strlen(input) == 0) continue;

        Message msg;
        
        // Команда выхода
        if (strcmp(input, "/quit") == 0) {
            msg.type = MSG_BYE;
            strncpy(msg.payload, "Bye", MAX_PAYLOAD);
            send(sock, &msg, sizeof(Message), 0);
            break;
        }
        
        // Команда пинга
        if (strcmp(input, "/ping") == 0) {
            msg.type = MSG_PING;
            strncpy(msg.payload, "ping", MAX_PAYLOAD);
            send(sock, &msg, sizeof(Message), 0);
            continue;
        }

        // Личное сообщение /w nick message
        if (strncmp(input, "/w ", 3) == 0) {
            msg.type = MSG_PRIVATE;
            // Копируем всё после "/w "
            strncpy(msg.payload, input + 3, MAX_PAYLOAD);
            send(sock, &msg, sizeof(Message), 0);
            continue;
        }

        // Обычное сообщение
        msg.type = MSG_TEXT;
        strncpy(msg.payload, input, MAX_PAYLOAD);
        send(sock, &msg, sizeof(Message), 0);
    }

    close(sock);
    return 0;
}