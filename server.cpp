#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8888

struct Message {
    uint32_t len;
    uint8_t type;
    char data[1024];
};

enum { HELLO = 1, WELCOME = 2, TEXT = 3, PING = 4, PONG = 5, BYE = 6 };

int main() {
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
   
    bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr));
    
   
    listen(server_fd, 1);
    std::cout << "Server started on port " << PORT << std::endl;
    
    
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
    
    char client_ip[16];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, 16);
    int client_port = ntohs(client_addr.sin_port);
    std::cout << "Client connected: " << client_ip << ":" << client_port << std::endl;
    
    
    Message msg;
    recv(client_fd, &msg.len, 4, 0);
    recv(client_fd, &msg.type, 1, 0);
    int total_len = ntohl(msg.len);
    int data_len = total_len - 1;
    recv(client_fd, msg.data, data_len, 0);
    msg.data[data_len] = '\0';
    
    std::cout << "[" << client_ip << ":" << client_port << "]: Hello (" << msg.data << ")" << std::endl;
    
    
    Message welcome;
    welcome.type = WELCOME;
    std::string welcome_text = std::string(client_ip) + ":" + std::to_string(client_port);
    welcome.len = htonl(1 + welcome_text.length() + 1);
    strcpy(welcome.data, welcome_text.c_str());
    send(client_fd, &welcome, 4 + 1 + welcome_text.length() + 1, 0);
    
   
    while (true) {
        
        int bytes = recv(client_fd, &msg.len, 4, 0);
        if (bytes <= 0) {
            std::cout << "Client disconnected" << std::endl;
            break;
        }
        
        recv(client_fd, &msg.type, 1, 0);
        total_len = ntohl(msg.len);
        data_len = total_len - 1;
        
        if (data_len > 0) {
            recv(client_fd, msg.data, data_len, 0);
            msg.data[data_len] = '\0';
        }
        
        
        if (msg.type == TEXT) {
            std::cout << "[" << client_ip << ":" << client_port << "]: " << msg.data << std::endl;
        }
        else if (msg.type == PING) {
            std::cout << "Received PING, sending PONG" << std::endl;
            
            Message pong;
            pong.type = PONG;
            pong.len = htonl(1);
            send(client_fd, &pong, 4 + 1, 0);
        }
        else if (msg.type == BYE) {
            std::cout << "Client disconnected (BYE)" << std::endl;
            break;
        }
    }
    
    close(client_fd);
    close(server_fd);
    return 0;
}