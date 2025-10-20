#include <iostream>
#include <cstring>
#include <string>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>

using namespace std;

// --- تنظیمات ثابت (Const Settings) ---
const int PORT = 8080;
const int BACKLOG = 10;
const int BUFFER_SIZE = 1024;
const string WEB_ROOT = "www"; // پوشه‌ی اصلی فایل‌های استاتیک

// --- منابع عمومی و همزمان ---
// استفاده از atomic برای شمارنده ساده و امن در محیط چندنخی (Thread-Safe)
atomic<int> counter(0); 
mutex cout_mutex; // برای امن کردن خروجی استاندارد (cout)

/**
 * @brief  ساخت پاسخ ساده HTTP
 * @param content محتوای بدنه (Body) پاسخ
 * @param status_code کد وضعیت (مثلا 200 یا 404)
 * @param content_type نوع محتوا
 * @return رشته کامل پاسخ HTTP
 */
string build_http_response(const string& content, int status_code, const string& content_type = "text/html") {
    stringstream response;
    string status_text = (status_code == 200) ? "OK" : ((status_code == 404) ? "Not Found" : "Internal Server Error");
    
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << content.length() << "\r\n";
    response << "Connection: close\r\n"; 
    response << "\r\n"; 
    response << content;
    
    return response.str();
}

/**
 * @brief  تشخیص نوع محتوا (MIME Type) بر اساس پسوند فایل
 * @param file_path مسیر کامل فایل
 * @return رشته MIME Type مناسب
 */
string get_mime_type(const string& file_path) {
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos == string::npos) return "text/plain"; 

    string ext = file_path.substr(dot_pos + 1);

    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "png") return "image/png";
    if (ext == "gif") return "image/gif";
    if (ext == "ico") return "image/x-icon";
    return "application/octet-stream"; // پیش‌فرض برای سایر فایل‌ها
}

/**
 * @brief  سرو کردن فایل‌های استاتیک از پوشه‌ی www
 * @param client_socket سوکت
 * @param path مسیر درخواستی
 */
void serve_static_file(int client_socket, const string& path) {
    string full_path = WEB_ROOT + path;
    
    ifstream file(full_path, ios::binary | ios::ate);
    
    if (!file.is_open()) {
        string content = "<h1>404 - پیدا نشد</h1><p>فایل در سرور پیدا نشد.</p>";
        string response_str = build_http_response(content, 404);
        send(client_socket, response_str.c_str(), response_str.length(), 0);
        return;
    }
    
    // خواندن فایل
    streampos file_size = file.tellg();
    file.seekg(0, ios::beg);
    string content(file_size, ' ');
    file.read(&content[0], file_size);

    // ساخت پاسخ
    string mime_type = get_mime_type(full_path);
    string response_str = build_http_response(content, 200, mime_type);
    
    send(client_socket, response_str.c_str(), response_str.length(), 0);
}

/**
 * @brief  هندلر اصلی برای مدیریت درخواست‌های کلاینت
 * @param client_socket سوکت اتصال کلاینت
 */
void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE] = {0};
    string response_str;
    
    // ۱. دریافت درخواست
    long valread = read(client_socket, buffer, BUFFER_SIZE - 1);
    
    if (valread > 0) {
        string request(buffer);
        
        // تشخیص متد و مسیر درخواست
        size_t method_end = request.find(' ');
        size_t path_start = method_end + 1;
        size_t path_end = request.find(' ', path_start);
        
        if (method_end != string::npos && path_end != string::npos) {
            string method = request.substr(0, method_end);
            string full_path = request.substr(path_start, path_end - path_start);

            // <--- جدا کردن مسیر اصلی از Query String
            string path;
            string query_string;
            size_t query_pos = full_path.find('?');
            
            if (query_pos != string::npos) {
                path = full_path.substr(0, query_pos);
                query_string = full_path.substr(query_pos + 1);
            } else {
                path = full_path;
            }
            // --->

            // نمایش درخواست در کنسول (با قفل برای عدم تداخل)
            {
                lock_guard<mutex> lock(cout_mutex);
                cout << "درخواست از " << this_thread::get_id() << ": " << method << " " << path << (query_string.empty() ? "" : ("?" + query_string)) << endl;
            }

            // ۲. منطق مسیردهی (Routing)
            if (path == "/") {
                string content = 
                    "<!DOCTYPE html>"
                    "<html><head><title>Minimal C++ Server</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
                    "<h1>خوش آمدید، ای فرمانده!</h1>"
                    "<p>این وب سرور مینیمال با Multithreading است.</p>"
                    "<p>برای دیدن شمارنده: <a href=\"/count\">/count</a></p>"
                    "<p>برای تست Query String: <a href=\"/hello?name=Gemini\">/hello?name=Gemini</a></p>"
                    "</body></html>";
                response_str = build_http_response(content, 200);
            } 
            else if (path == "/count") {
                int current_count = ++counter; 
                
                string content = 
                    "<!DOCTYPE html>"
                    "<html><head><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
                    "<h1>شمارنده</h1>"
                    "<p>این صفحه " + to_string(current_count) + " بار بازدید شده است.</p>"
                    "<p>آزمون اتصال موازی با موفقیت انجام شد.</p>"
                    "</body></html>";
                response_str = build_http_response(content, 200);
            } 
            else if (path == "/hello") {
                string name = "دوست ناشناس"; // مقدار پیش‌فرض
                
                // <--- Parsing بسیار ساده Query String
                size_t name_start = query_string.find("name=");
                if (name_start != string::npos) {
                    size_t value_start = name_start + 5; 
                    size_t value_end = query_string.find('&', value_start);
                    if (value_end == string::npos) {
                        value_end = query_string.length();
                    }
                    name = query_string.substr(value_start, value_end - value_start);
                }
                // --->

                string content = 
                    "<!DOCTYPE html>"
                    "<html><head><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
                    "<h1>سلام، " + name + "!</h1>"
                    "<p>داده‌های ارسالی با موفقیت خوانده شدند.</p>"
                    "</body></html>";
                response_str = build_http_response(content, 200);
            }
            else if (path.find('.') != string::npos) { // سرو فایل‌های استاتیک
                serve_static_file(client_socket, path);
                return; // پس از سرو فایل، از تابع خارج شوید
            }
            else {
                // ۴۰۴ - پیدا نشد
                string content = "<h1>404 - پیدا نشد</h1><p>مسیر مورد نظر وجود ندارد.</p>";
                response_str = build_http_response(content, 404);
            }
        }
    }
    
    // ۳. ارسال پاسخ و بستن سوکت (فقط اگر پاسخ توسط serve_static_file ارسال نشده باشد)
    if (!response_str.empty()) {
        send(client_socket, response_str.c_str(), response_str.length(), 0);
    }
    
    close(client_socket);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    // ۱. ایجاد سوکت
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(PORT);
       
    // ۲. اتصال سوکت به آدرس و پورت
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
       
    // ۳. گوش دادن برای اتصال‌ها
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    cout << "وب سرور C++ شما در حال اجرا است. پورت: " << PORT << endl;
    cout << "شروع به پذیرش اتصالات موازی با Threading..." << endl;
    
    while (true) {
        // ۴. پذیرش اتصال جدید (بلوکه می‌کند تا یک کلاینت وصل شود)
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue; 
        }
        
        // ۵. ارسال اتصال به یک نخ (Thread) جدید برای پردازش موازی
        thread client_thread(handle_client, new_socket);
        client_thread.detach(); 
    }
    
    return 0;
}
