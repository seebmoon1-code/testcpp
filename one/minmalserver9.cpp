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
#include <sys/stat.h>
#include <sys/types.h>
#include <map>
#include <time.h>
#include <iomanip>
#include <dirent.h>
#include <errno.h>
#include <vector> // NEW: برای مدیریت ایمن‌تر حافظه
#include <memory> // NEW: برای smart pointerها (اختیاری)

using namespace std;

// --- تنظیمات ثابت (Const Settings) ---
const int PORT = 8080;
const int BACKLOG = 10;
const int BUFFER_SIZE = 4096;
const string WEB_ROOT = "www";
const string UPLOAD_ROOT = "uploads";

// --- منابع عمومی و همزمان ---
atomic<int> counter(0);
mutex cout_mutex;

// --- ساختار ذخیره داده API (مانند دیتابیس) ---
map<int, map<string, string>> users_db; // {ID: {Key: Value}}
atomic<int> next_user_id(1);

// --- توابع کمکی JSON (بدون تغییر، اما با آگاهی از ضعف) ---
string map_to_json(const map<string, string>& data) {
    string json = "{";
    for (const auto& pair : data) {
        json += "\"" + pair.first + "\": \"" + pair.second + "\",";
    }
    if (json.length() > 1) { 
        json.pop_back();
    }
    json += "}";
    return json;
}

map<string, string> parse_simple_json(const string& json_str) {
    map<string, string> data;
    size_t start = json_str.find('{');
    size_t end = json_str.find('}');
    if (start == string::npos || end == string::npos || end <= start) return data;

    string content = json_str.substr(start + 1, end - start - 1);
    stringstream ss(content);
    string segment;

    while (getline(ss, segment, ',')) {
        size_t colon = segment.find(':');
        if (colon != string::npos) {
            string key = segment.substr(0, colon);
            string value = segment.substr(colon + 1);
            
            key.erase(remove(key.begin(), key.end(), '\"'), key.end());
            key.erase(remove(key.begin(), key.end(), ' '), key.end());
            value.erase(remove(value.begin(), value.end(), '\"'), value.end());
            value.erase(remove(value.begin(), value.end(), ' '), value.end());

            if (!key.empty() && !value.empty()) {
                data[key] = value;
            }
        }
    }
    return data;
}

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

// تابع اصلی ساخت پاسخ HTTP (برای محتوای داینامیک)
string build_http_response(const string& content, int status_code, const string& content_type = "text/html") {
    stringstream response;
    string status_text = (status_code == 200) ? "OK" : ((status_code == 201) ? "Created" : ((status_code == 404) ? "Not Found" : ((status_code == 400) ? "Bad Request" : "Internal Server Error")));
    
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << content.length() << "\r\n";
    response << "Connection: keep-alive\r\n";
    response << "\r\n";
    response << content;
    
    return response.str();
}

// تابع ساخت پاسخ با قابلیت کش (برای فایل‌های استاتیک)
string build_http_response_cacheable(long file_size, const string& content_type) { // (تغییر) حالا فقط هدرها را می‌سازد
    stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << file_size << "\r\n";
    response << "Connection: keep-alive\r\n";
    response << "Cache-Control: public, max-age=604800\r\n"; 
    response << "\r\n";
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
    if (ext == "mp4") return "video/mp4";
    
    return "application/octet-stream";
}

// --- (تغییر بزرگ: رفع ضعف ۳) سرویس‌دهی فایل به صورت Chunked ---
void serve_static_file(int client_socket, const string& full_path) { 
    ifstream file(full_path, ios::binary | ios::ate);
    
    if (!file.is_open()) {
        string content = "<h1>404 - پیدا نشد</h1><p>فایل در سرور پیدا نشد.</p>";
        string response_str = build_http_response(content, 404);
        send(client_socket, response_str.c_str(), response_str.length(), 0);
        return;
    }
    
    long file_size = file.tellg();
    file.seekg(0, ios::beg);

    string mime_type = get_mime_type(full_path);
    
    // ۱. ارسال هدرها
    string response_headers = build_http_response_cacheable(file_size, mime_type);
    send(client_socket, response_headers.c_str(), response_headers.length(), 0);
    
    // ۲. ارسال محتوا در قطعات کوچک (Chunking)
    vector<char> buffer(BUFFER_SIZE); // استفاده از vector برای مدیریت ایمن
    
    while (file.read(buffer.data(), buffer.size())) {
        send(client_socket, buffer.data(), file.gcount(), 0);
    }
    // ارسال باقی مانده (اگر فایل مضربی از BUFFER_SIZE نباشد)
    if (file.gcount() > 0) {
        send(client_socket, buffer.data(), file.gcount(), 0);
    }
}
// --- پایان تغییر ۳ ---

// --- تابع فهرست‌بندی فایل‌ها و فرم آپلود (بدون تغییر) ---
string list_files(const string& upload_dir) {
    string html_content = 
        "<!DOCTYPE html><html><head><title>مدیریت فایل</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
        "<h1>مدیریت فایل‌ها (پایدار)</h1>"
        "<h2>فایل‌های آپلود شده</h2><ul>";

    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(upload_dir.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            string filename = ent->d_name;
            if (filename != "." && filename != "..") {
                html_content += "<li><a href=\"/files/" + filename + "\">" + filename + "</a></li>";
            }
        }
        closedir(dir);
    } else {
        html_content += "<li>خطا: قادر به باز کردن پوشه‌ی آپلود نیست.</li>";
    }

    html_content += "</ul>";
    
    html_content +=
        "<h2>آپلود فایل جدید</h2>"
        "<input type=\"file\" id=\"fileInput\" required>"
        "<button onclick=\"uploadFile()\">آپلود</button>"
        "<p>توجه: این سرور از روش ساده (octet-stream) برای آپلود استفاده می‌کند.</p>"
        "<script>"
        "function uploadFile() {"
            "const fileInput = document.getElementById('fileInput');"
            "const file = fileInput.files[0];"
            "if (file) {"
                "const xhr = new XMLHttpRequest();"
                "xhr.open('POST', '/upload', true);"
                "xhr.setRequestHeader('Content-Type', 'application/octet-stream');" 
                "xhr.onload = function() {"
                    "alert('آپلود موفقیت‌آمیز');"
                    "window.location.reload();"
                "};"
                "xhr.send(file);"
            "} else { alert('لطفا یک فایل انتخاب کنید.'); }"
        "}"
        "</script>"
        "<p><a href=\"/\">بازگشت به صفحه اصلی</a></p>"
        "</body></html>";
    
    return html_content;
}

// --- تابع هندل کردن آپلود فایل (ساده) ---
void handle_upload(int client_socket, const string& body, const map<string, string>& headers) {
    // ... (منطق آپلود) ...
    // ... (برای سادگی این بخش دست نخورده است، اما باید با chunking بهبود یابد)
    // ...
    // ۱. تولید نام فایل منحصر به فرد
    stringstream ss;
    time_t timer;
    time(&timer);
    ss << UPLOAD_ROOT << "/file_" << std::put_time(std::localtime(&timer), "%Y%m%d%H%M%S") << "_" << rand() % 1000 << ".bin";
    string filename = ss.str();
    
    // ۲. ذخیره کردن کل بدنه درخواست در فایل
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


// --- هندلر اصلی برای مدیریت درخواست‌های کلاینت ---
void handle_client(int client_socket) {
    struct timeval timeout;
    timeout.tv_sec = 5; 
    timeout.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);

    // تغییر ۱: استفاده از vector به جای char[] در حافظه موقت اولیه
    vector<char> buffer(BUFFER_SIZE); 
    long valread;
    
    // حلقه Keep-Alive
    do { 
        valread = read(client_socket, buffer.data(), BUFFER_SIZE - 1); // استفاده از .data()
        
        if (valread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break; 
        } else if (valread <= 0) {
            break;
        }

        string request(buffer.data());
        
        // بقیه کد برای parse کردن...
        size_t first_line_end = request.find("\r\n");
        if (first_line_end == string::npos) break;

        string first_line = request.substr(0, first_line_end);
        size_t method_end = first_line.find(' ');
        size_t path_start = method_end + 1;
        size_t path_end = first_line.find(' ', path_start);
        
        if (method_end == string::npos || path_end == string::npos) break;
        
        string method = first_line.substr(0, method_end);
        string full_path_with_query = first_line.substr(path_start, path_end - path_start);

        string path;
        size_t query_pos = full_path_with_query.find('?');
        
        if (query_pos != string::npos) {
            path = full_path_with_query.substr(0, query_pos);
        } else {
            path = full_path_with_query;
        }

        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "درخواست از " << this_thread::get_id() << ": " << method << " " << path << endl;
        }

        // --- ۲. استخراج هدرها و بدنه ---
        map<string, string> headers;
        size_t headers_end = request.find("\r\n\r\n");
        string body;

        if (headers_end != string::npos) {
            // ... (پارس کردن هدرها) ...
            string headers_raw = request.substr(first_line_end + 2, headers_end - first_line_end - 2);
            stringstream ss(headers_raw);
            string line;
            while (getline(ss, line, '\r')) {
                if (line.empty()) continue; 
                size_t colon = line.find(':');
                if (colon != string::npos) {
                    string key = line.substr(0, colon);
                    string value = line.substr(colon + 2);
                    if (value.back() == '\n') value.pop_back(); 
                    transform(key.begin(), key.end(), key.begin(), ::tolower);
                    headers[key] = value;
                }
            }
            body = request.substr(headers_end + 4);
        }

        // --- ۳. منطق مسیردهی (Routing) ---
        string response_str = ""; 

        if (method == "GET") {
            // ... (سایر مسیرهای GET) ...
            if (path == "/") {
                string content = 
                    "<!DOCTYPE html><html><head><title>Minimal Server</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
                    "<h1>فرمانده، خوش آمدید!</h1>"
                    "<p>برای مدیریت فایل‌ها (آپلود/دانلود)، به <a href=\"/files\">/files</a> بروید.</p>"
                    "<p>شمارنده: <a href=\"/count\">/count</a></p>"
                    "<p>تست API (JSON): <a href=\"/api/users\">/api/users</a> (برای ایجاد کاربر جدید از POST استفاده کنید)</p>"
                    "</body></html>";
                response_str = build_http_response(content, 200);
            } 
            else if (path == "/count") {
                int current_count = ++counter; 
                string content = "<h1>شمارنده</h1><p>صفحه " + to_string(current_count) + " بار بازدید شده است.</p>";
                response_str = build_http_response(content, 200);
            } 
            else if (path == "/files") { 
                response_str = build_http_response(list_files(UPLOAD_ROOT), 200);
            }
            else if (path.rfind("/files/", 0) == 0) { 
                string filename = path.substr(7); 
                string full_path = UPLOAD_ROOT + "/" + filename;
                
                serve_static_file(client_socket, full_path); // (تغییر)
                response_str = "SERVED"; 
            }
            else if (path == "/api/users") {
                string all_users_json = "[\n";
                
                for (const auto& entry : users_db) {
                    all_users_json += map_to_json(entry.second) + ",\n";
                }
                
                if (users_db.size() > 0) { 
                    all_users_json.pop_back(); 
                    all_users_json.pop_back(); 
                }
                
                all_users_json += "\n]";
                
                response_str = build_http_response(all_users_json, 200, "application/json"); 
            }
            else if (path.find('.') != string::npos) { 
                string full_path = WEB_ROOT + sanitize_path(path);
                serve_static_file(client_socket, full_path); // (تغییر)
                response_str = "SERVED"; 
            }
            else {
                response_str = build_http_response("<h1>404</h1><p>مسیر مورد نظر وجود ندارد.</p>", 404);
            }
        } 
        else if (method == "POST") {
            if (path == "/upload") {
                if (headers.count("content-length")) {
                    try {
                        int content_length = stoi(headers["content-length"]);
                        if (body.length() < content_length) {
                            // --- (تغییر بزرگ: رفع ضعف ۱) مدیریت حافظه برای خواندن باقیمانده ---
                            size_t remaining_to_read = content_length - body.length();
                            // استفاده از unique_ptr برای تضمین حذف حافظه در هر حالت (RAII)
                            unique_ptr<char[]> remaining_buffer(new char[remaining_to_read + 1]); 
                            remaining_buffer[remaining_to_read] = '\0';
                            
                            size_t total_read = 0;
                            while(remaining_to_read > 0) {
                                size_t read_now = read(client_socket, remaining_buffer.get() + total_read, remaining_to_read);
                                if (read_now <= 0) break;
                                
                                body.append(remaining_buffer.get() + total_read, read_now);
                                remaining_to_read -= read_now;
                                total_read += read_now;
                            }
                            // حافظه به صورت خودکار توسط unique_ptr حذف می‌شود، حتی اگر خطا رخ دهد
                            // --- پایان تغییر ۱ ---
                        }
                        handle_upload(client_socket, body, headers);
                    } catch (const exception& e) {
                        response_str = build_http_response("<h1>500</h1><p>خطا در Content-Length.</p>", 500);
                    }
                } else {
                     response_str = build_http_response("<h1>400</h1><p>هدر Content-Length لازم است.</p>", 400);
                }
            }
            else if (path == "/api/users") {
                try {
                    map<string, string> new_user_data = parse_simple_json(body);
                    
                    if (new_user_data.count("name") && new_user_data.count("email")) {
                        int id = next_user_id.fetch_add(1);
                        new_user_data["id"] = to_string(id);

                        users_db[id] = new_user_data;
                        
                        string response_json = map_to_json(new_user_data);

                        response_str = build_http_response(response_json, 201, "application/json"); 
                        
                        {
                            lock_guard<mutex> lock(cout_mutex);
                            cout << "کاربر جدید ایجاد شد: ID " << id << endl;
                        }

                    } else {
                        response_str = build_http_response("{\"error\": \"Name and email fields are required.\"}", 400, "application/json");
                    }
                } catch (const exception& e) {
                    response_str = build_http_response("{\"error\": \"Invalid JSON format.\"}", 400, "application/json");
                }
            }
            else {
                response_str = build_http_response("<h1>404</h1><p>مسیر POST نامعتبر است.</p>", 404);
            }
        }
        
        // ارسال پاسخ 
        if (response_str != "" && response_str != "SERVED") {
            send(client_socket, response_str.c_str(), response_str.length(), 0);
        }
        
    } while (true); 
    
    close(client_socket);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    // ۱. ایجاد پوشه آپلود و تنظیمات سوکت... (بدون تغییر)
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
       
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
       
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    srand(time(NULL));
    cout << "وب سرور C++ شما در حال اجرا است. پورت: " << PORT << endl;
    
    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue; 
        }
        
        // ۴. ایجاد نخ جدید
        thread client_thread(handle_client, new_socket);
        client_thread.detach(); 
    }
    
    return 0;
}
