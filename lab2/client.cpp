#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8888
#define SERVER_IP "127.0.0.1"

struct Message {
    uint32_t len;
    uint8_t type;
    char data[1024];
};

enum { HELLO = 1, WELCOME = 2, TEXT = 3, PING = 4, PONG = 5, BYE = 6 };

int main() {
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
    
    
    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cout << "Connection failed" << std::endl;
        return 1;
    }
    
    std::cout << "Connected to server" << std::endl;
    
  
    std::string nickname;
    std::cout << "Enter nickname: ";
    std::getline(std::cin, nickname);
    
   
    Message hello;
    hello.type = HELLO;
    hello.len = htonl(1 + nickname.length() + 1);
    strcpy(hello.data, nickname.c_str());
    send(sock, &hello, 4 + 1 + nickname.length() + 1, 0);
    
   
    Message welcome;
    recv(sock, &welcome.len, 4, 0);
    recv(sock, &welcome.type, 1, 0);
    int total_len = ntohl(welcome.len);
    int data_len = total_len - 1;
    recv(sock, welcome.data, data_len, 0);
    welcome.data[data_len] = '\0';
    
    std::cout << "Welcome " << welcome.data << std::endl;
    
    
    std::string input;
    bool running = true;
    
    while (running) {
        std::cout << "> ";
        std::getline(std::cin, input);
        
        if (input == "/quit") {
            
            Message bye;
            bye.type = BYE;
            bye.len = htonl(1);
            send(sock, &bye, 4 + 1, 0);
            running = false;
            break;
        }
        else if (input == "/ping") {
            
            Message ping;
            ping.type = PING;
            ping.len = htonl(1);
            send(sock, &ping, 4 + 1, 0);
            
            
            Message pong;
            recv(sock, &pong.len, 4, 0);
            recv(sock, &pong.type, 1, 0);
            if (pong.type == PONG) {
                std::cout << "PONG" << std::endl;
            }
        }
        else if (!input.empty()) {
           
            Message text;
            text.type = TEXT;
            text.len = htonl(1 + input.length() + 1);
            strcpy(text.data, input.c_str());
            send(sock, &text, 4 + 1 + input.length() + 1, 0);
        }
    }
    
    close(sock);
    std::cout << "Disconnected" << std::endl;
    return 0;
}