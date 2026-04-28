#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#define MAX_PAYLOAD  256
#define MAX_NAME     32
#define MAX_TIME_STR 64
#define PORT         8080
#define MAX_CLIENTS  64
#define HISTORY_FILE "chat_history.json"

// Типы сообщений
typedef enum {
    MSG_HELLO        = 1,
    MSG_WELCOME      = 2,
    MSG_TEXT         = 3,
    MSG_PING         = 4,
    MSG_PONG         = 5,
    MSG_BYE          = 6,
    MSG_AUTH         = 7,
    MSG_PRIVATE      = 8,
    MSG_ERROR        = 9,
    MSG_SERVER_INFO  = 10,
    MSG_LIST         = 11,
    MSG_HISTORY      = 12,
    MSG_HISTORY_DATA = 13,
    MSG_HELP         = 14
} MessageType;

// Расширенная структура сообщения (ЛР5)
typedef struct {
    uint32_t length;                 // Общая длина структуры или полезной нагрузки
    uint8_t  type;                   // Тип сообщения
    uint32_t msg_id;                 // Уникальный ID сообщения
    char     sender[MAX_NAME];       // Ник отправителя
    char     receiver[MAX_NAME];     // Ник получателя (пустой, если broadcast)
    time_t   timestamp;              // Время создания
    char     payload[MAX_PAYLOAD];   // Текст сообщения
} MessageEx;

// Структура клиента
typedef struct {
    int sock;
    char nickname[MAX_NAME];
    int authenticated;
    int active;
} Client;

// Структура для офлайн-сообщения (хранится в памяти до доставки)
typedef struct {
    char sender[MAX_NAME];
    char receiver[MAX_NAME];
    char text[MAX_PAYLOAD];
    time_t timestamp;
    uint32_t msg_id;
    int delivered; // Флаг доставки
} OfflineMsg;

#endif
