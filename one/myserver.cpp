
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

using namespace std;

// پورت مورد نظر برای ارتباط
#define PORT 8080 

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    
    // ۱. ایجاد سوکت
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // ۲. تنظیمات آدرس سرور
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // گوش دادن روی تمام اینترفیس‌ها
    address.sin_port = htons(PORT);
    
    // ۳. اتصال سوکت به آدرس و پورت (Bind)
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // ۴. شروع گوش دادن (Listen)
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    cout << "Server listening on port " << PORT << "..." << endl;
    
    // ۵. پذیرش اتصال جدید (Accept)
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }
    
    // ۶. دریافت داده‌ها (Read)
    read(new_socket, buffer, 1024);
    cout << "Message received from phone: " << buffer << endl;
    
    // ۷. بستن سوکت‌ها
    close(new_socket);
    close(server_fd);
    
    return 0;
}
