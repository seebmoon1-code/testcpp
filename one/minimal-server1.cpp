#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

using namespace std;

// پورت مورد نظر. پورت 8080 نیاز به دسترسی 'root' ندارد.
const int PORT = 8080;

// محتوای HTML که می‌خواهید گوشی‌ها ببینند.
const char* html_content =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 130\r\n" // طول محتوای HTML (به دقت محاسبه شود!)
    "Connection: close\r\n"
    "\r\n" // خط خالی برای جداسازی هدرها از محتوا
    "<!DOCTYPE html>"
    "<html><body>"
    "<h1>سلام از لینوکس مینت!</h1>"
    "<p>این محتوای مینیمال وب سرور C++ من است. پر قو!</p>"
    "</body></html>";

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    // 1. ایجاد سوکت
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
       
    // تنظیم آدرس سرور (برای اتصال در شبکه محلی)
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // گوش دادن روی همه رابط‌ها (LAN)
    address.sin_port = htons(PORT);
       
    // 2. اتصال سوکت به آدرس و پورت
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
       
    // 3. گوش دادن برای اتصال‌ها (با صف انتظار ۳)
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    cout << "وب سرور C++ مینیمال من در حال اجرا است. پورت: " << PORT << endl;

    // 8. حلقه اصلی سرور
    while (true) {
        cout << "\nمنتظر اتصال جدید..." << endl;

        // 4. پذیرش اتصال جدید (بلوکه می‌کند تا یک کلاینت وصل شود)
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue; // ادامه به حلقه بعدی در صورت خطا
        }
           
        // 5. دریافت درخواست HTTP
        long valread = read(new_socket, buffer, 1024);
        // نمایش بخش کوچکی از درخواست کلاینت
        cout << "درخواست دریافتی:" << endl;
        // مطمئن می‌شویم که رشته null-terminated است
        buffer[valread] = '\0'; 
        cout << buffer << endl;
           
        // 6. ارسال پاسخ HTTP
        write(new_socket, html_content, strlen(html_content));
        cout << "پاسخ HTML ارسال شد." << endl;
           
        // 7. بستن سوکت کلاینت (برای HTTP ساده)
        close(new_socket);
    }
    
    // این قسمت هرگز اجرا نمی‌شود مگر اینکه برنامه متوقف شود.
    return 0;
}
