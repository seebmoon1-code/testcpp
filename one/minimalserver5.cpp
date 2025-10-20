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
#include <algorithm>
#include <sys/stat.h> // برای mkdir
#include <sys/types.h>
#include <map>
#include <time.h>   // برای تولید نام فایل تصادفی
#include <iomanip>  // برای تولید نام فایل تصادفی

using namespace std;

// --- تنظیمات ثابت (Const Settings) ---
const int PORT = 8080;
const int BACKLOG = 10;
const int BUFFER_SIZE = 4096; // افزایش اندازه بافر برای راحتی در خواندن هدرها
const string WEB_ROOT = "www";
const string UPLOAD_ROOT = "uploads"; // <--- پوشه جدید برای ذخیره فایل‌ها

// --- منابع عمومی و همزمان ---
atomic<int> counter(0);
mutex cout_mutex;

// --- توابع کمکی امنیتی و پروتکلی ---

string sanitize_path(string path) {
    size_t pos = path.find("..");
    while (pos != string::npos) {
        path.replace(pos, 2, ".");
        pos = path.find("..", pos);
    }
    if (path.empty() || path[0] != '/') {
        path.insert(0, "/");
    }
    return path;
}

string build_http_response(const string& content, int status_code, const string& content_type = "text/html") {
    stringstream response;
    string status_text = (status_code == 200) ? "OK" : ((status_code == 404) ? "Not Found" : "Internal Server Error");
    
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << content.length() << "\r\n";
    response << "Connection: keep-alive\r\n";
    response << "\r\n";
    response << content;
    
    return response.str();
}

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
    return "application/octet-stream";
}

void serve_static_file(int client_socket, const string& path) {
    string safe_path = sanitize_path(path);
    string full_path = WEB_ROOT + safe_path;
    
    ifstream file(full_path, ios::binary | ios::ate);
    
    if (!file.is_open()) {
        string content = "<h1>404 - پیدا نشد</h1><p>فایل در سرور پیدا نشد.</p>";
        string response_str = build_http_response(content, 404);
        send(client_socket, response_str.c_str(), response_str.length(), 0);
        return;
    }
    
    streampos file_size = file.tellg();
    file.seekg(0, ios::beg);
    string content(file_size, ' ');
    file.read(&content[0], file_size);

    string mime_type = get_mime_type(full_path);
    string response_str = build_http_response(content, 200, mime_type);
    
    send(client_socket, response_str.c_str(), response_str.length(), 0);
}

// --- تابع جدید: هندل کردن آپلود فایل (ساده) ---
void handle_upload(int client_socket, const string& body, const string& path, const map<string, string>& headers) {
    // ۱. تولید نام فایل منحصر به فرد (با استفاده از زمان)
    stringstream ss;
    time_t timer;
    time(&timer);
    ss << UPLOAD_ROOT << "/file_" << std::put_time(std::localtime(&timer), "%Y%m%d%H%M%S") << "_" << rand() % 1000 << ".bin";
    string filename = ss.str();

    // ۲. ذخیره کردن کل بدنه درخواست در فایل
    // توجه: در یک وب سرور واقعی، باید Multipart/form-data را Parse کنید.
    // اما برای مینیمالیسم، فرض میکنیم بدنه، خود داده فایل است.
    
    ofstream outfile(filename, ios::binary);
    if (outfile.is_open()) {
        outfile.write(body.c_str(), body.length());
        outfile.close();

        string content = "<h1>آپلود موفقیت‌آمیز</h1><p>فایل با موفقیت در " + filename + " ذخیره شد.</p>";
        string response_str = build_http_response(content, 200);
        send(client_socket, response_str.c_str(), response_str.length(), 0);
        
        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "فایل ذخیره شد: " << filename << endl;
        }

    } else {
        string content = "<h1>خطای سرور</h1><p>نمی‌توان فایل را ذخیره کرد.</p>";
        string response_str = build_http_response(content, 500);
        send(client_socket, response_str.c_str(), response_str.length(), 0);
    }
}


/**
 * @brief  هندلر اصلی برای مدیریت درخواست‌های کلاینت
 */
void handle_client(int client_socket) {
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);

    char buffer[BUFFER_SIZE] = {0};
    long valread;
    
    // حلقه برای مدیریت چند درخواست متوالی (Keep-Alive)
    do { 
        valread = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (valread <= 0) break;

        string request(buffer);
        
        // --- ۱. Parse کردن خط اول (متد، مسیر) ---
        size_t first_line_end = request.find("\r\n");
        string first_line = request.substr(0, first_line_end);

        size_t method_end = first_line.find(' ');
        size_t path_start = method_end + 1;
        size_t path_end = first_line.find(' ', path_start);
        
        if (method_end == string::npos || path_end == string::npos) break;
        
        string method = first_line.substr(0, method_end);
        string full_path = first_line.substr(path_start, path_end - path_start);

        string path;
        string query_string;
        size_t query_pos = full_path.find('?');
        
        if (query_pos != string::npos) {
            path = full_path.substr(0, query_pos);
            query_string = full_path.substr(query_pos + 1);
        } else {
            path = full_path;
        }

        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "درخواست از " << this_thread::get_id() << ": " << method << " " << path << endl;
        }

        // --- ۲. استخراج هدرها و بدنه (برای POST) ---
        map<string, string> headers;
        size_t headers_end = request.find("\r\n\r\n");
        string body;

        if (headers_end != string::npos) {
            string headers_raw = request.substr(first_line_end + 2, headers_end - first_line_end - 2);
            size_t current = 0;
            size_t next = headers_raw.find("\r\n", current);
            while (next != string::npos) {
                string line = headers_raw.substr(current, next - current);
                size_t colon = line.find(':');
                if (colon != string::npos) {
                    string key = line.substr(0, colon);
                    string value = line.substr(colon + 2);
                    // تبدیل کلید به حروف کوچک برای جستجوی آسان
                    transform(key.begin(), key.end(), key.begin(), ::tolower); 
                    headers[key] = value;
                }
                current = next + 2;
                next = headers_raw.find("\r\n", current);
            }
            
            // استخراج بدنه
            body = request.substr(headers_end + 4);
        }

        // --- ۳. منطق مسیردهی (Routing) ---
        string response_str = ""; 

        if (method == "GET") {
            if (path == "/") {
                string content = 
                    "<!DOCTYPE html><html><head><title>Minimal Server</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
                    "<h1>فرمانده، خوش آمدید!</h1>"
                    "<p>برای آپلود فایل، به <a href=\"/upload.html\">/upload.html</a> بروید.</p>"
                    "<p>شمارنده: <a href=\"/count\">/count</a></p>"
                    "</body></html>";
                response_str = build_http_response(content, 200);
            } 
            else if (path == "/count") {
                int current_count = ++counter; 
                string content = "<h1>شمارنده</h1><p>صفحه " + to_string(current_count) + " بار بازدید شده است.</p>";
                response_str = build_http_response(content, 200);
            } 
            else if (path.find('.') != string::npos) { 
                serve_static_file(client_socket, path);
            }
            else {
                response_str = build_http_response("<h1>404</h1><p>مسیر مورد نظر وجود ندارد.</p>", 404);
            }
        } 
        else if (method == "POST") {
            if (path == "/upload") {
                // اگر هدر Content-Length وجود دارد و بدنه در بافر اولیه نیست، باید آن را کامل بخوانیم
                if (headers.count("content-length")) {
                    int content_length = stoi(headers["content-length"]);
                    if (body.length() < content_length) {
                        // نیاز به خواندن بقیه بدنه است
                        char* remaining_buffer = new char[content_length - body.length()];
                        int remaining_to_read = content_length - body.length();
                        int read_now = 0;
                        while(remaining_to_read > 0) {
                            read_now = read(client_socket, remaining_buffer, remaining_to_read);
                            if (read_now <= 0) break;
                            body.append(remaining_buffer, read_now);
                            remaining_to_read -= read_now;
                        }
                        delete[] remaining_buffer;
                    }
                }
                
                handle_upload(client_socket, body, path, headers);
            }
            else {
                response_str = build_http_response("<h1>404</h1><p>مسیر POST نامعتبر است.</p>", 404);
            }
        }
        
        // ارسال پاسخ (اگر توسط توابع serve_static_file یا handle_upload ارسال نشده باشد)
        if (!response_str.empty()) {
            send(client_socket, response_str.c_str(), response_str.length(), 0);
        }

        // تمیز کردن بافر و تلاش برای خواندن درخواست بعدی (Keep-Alive)
        memset(buffer, 0, BUFFER_SIZE);
        valread = read(client_socket, buffer, BUFFER_SIZE - 1);
        
    } while (valread > 0); // ادامه حلقه تا زمانی که داده دریافت شود یا Keep-Alive تایم‌اوت شود
    
    close(client_socket);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    // ۱. ایجاد پوشه آپلود و تنظیمات اولیه
    if (mkdir(UPLOAD_ROOT.c_str(), 0777) == -1 && errno != EEXIST) {
        perror("mkdir failed for uploads");
        exit(EXIT_FAILURE);
    }
    
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
       
    // ۲. اتصال سوکت
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
       
    // ۳. گوش دادن
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    srand(time(NULL)); // برای تولید نام فایل تصادفی
    cout << "وب سرور C++ شما در حال اجرا است. پورت: " << PORT << endl;
    
    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue; 
        }
        
        thread client_thread(handle_client, new_socket);
        client_thread.detach(); 
    }
    
    return 0;
}
