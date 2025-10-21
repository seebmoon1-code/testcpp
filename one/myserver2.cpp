#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <string>

using namespace std;

#define PORT 8080 
#define BUFFER_SIZE 1024
const string TEMP_FILENAME = "temp_code.txt"; // نام فایل موقت

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    ssize_t bytes_read;
    string final_filename;
    
    // ۱. ایجاد سوکت، Bind و Listen
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed"); exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed"); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        perror("listen"); exit(EXIT_FAILURE);
    }
    
    cout << "Server is ready. Listening on port " << PORT << "..." << endl;
    
    // ۲. پذیرش اتصال
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("accept"); exit(EXIT_FAILURE);
    }
    
    cout << "Phone connected. Receiving data..." << endl;
    
    // ۳. دریافت داده و ذخیره در فایل موقت
    ofstream outfile(TEMP_FILENAME.c_str());
    
    while ((bytes_read = read(new_socket, buffer, BUFFER_SIZE)) > 0) {
        outfile.write(buffer, bytes_read);
    }
    
    outfile.close();
    close(new_socket); // بستن سوکت کلاینت
    
    cout << "\n---------------------------------------" << endl;
    cout << "Data reception complete. Saved as **" << TEMP_FILENAME << "**" << endl;
    
    // ۴. پرسیدن نام نهایی از کاربر
    cout << "Enter the final filename (e.g., test.cpp): ";
    cin >> final_filename;

    // ۵. تغییر نام فایل
    if (rename(TEMP_FILENAME.c_str(), final_filename.c_str()) != 0) {
        perror("Error renaming file");
    } else {
        cout << "File saved successfully as: **" << final_filename << "**" << endl;
    }
    
    // ۶. بستن سرور
    close(server_fd);
    
    return 0;
}
