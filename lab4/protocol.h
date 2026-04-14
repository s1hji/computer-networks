#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <string.h>

#define MAX_PAYLOAD 1024
#define MAX_NICKNAME 32
#define MAX_CLIENTS 64
#define PORT 8080

// Типы сообщений согласно ЛР4
typedef enum {
    MSG_HELLO = 1,      // Для совместимости, но в ЛР4 основной вход через AUTH
    MSG_WELCOME = 2,
    MSG_TEXT = 3,
    MSG_PING = 4,
    MSG_PONG = 5,
    MSG_BYE = 6,
    MSG_AUTH = 7,       // Аутентификация
    MSG_PRIVATE = 8,    // Личное сообщение
    MSG_ERROR = 9,      // Ошибка
    MSG_SERVER_INFO = 10 // Системные сообщения
} MessageType;

// Структура сообщения
typedef struct {
    uint32_t length;    // Длина полезной нагрузки + тип (для простоты можно игнорировать при recv фикс. размера, но поле есть)
    uint8_t  type;
    char     payload[MAX_PAYLOAD];
} Message;

// Структура клиента для сервера
typedef struct {
    int sock;
    char nickname[MAX_NICKNAME];
    int authenticated;  // 1 если прошел аутентификацию
    int active;         // 1 если соединение активно
} Client;

#endif
