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
#include <vector>
#include <memory> 
#include <shared_mutex>
#include <functional> // برای HandlerFunc

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
map<int, map<string, string>> users_db; // {ID: {Key: Value}}
atomic<int> next_user_id(1);
shared_mutex db_mutex; // Shared Mutex برای کارایی Read/Write

// --- توابع کمکی پروتکلی (Forward Declarations) ---
string sanitize_path(string path);
string build_http_response(const string& content, int status_code, const string& content_type);
string build_http_response_cacheable(long file_size, const string& content_type);
string get_mime_type(const string& file_path);
void serve_static_file(int client_socket, const string& full_path);
string list_files(const string& upload_dir);
void handle_upload_stream(int client_socket, const string& initial_body, long content_length);

// ----------------------------------------------------------------------
// --- ۱. ابزارهای JSON (برای خوانایی و جایگزینی با کتابخانه استاندارد) ---
// ----------------------------------------------------------------------

class JsonParser {
public:
    // **بهبود یافته:** Stringify
    static string stringify(const map<string, string>& data) {
        string json = "{";
        for (const auto& pair : data) {
            json += "\"" + pair.first + "\": \"" + pair.second + "\",";
        }
        if (json.length() > 1) { 
            json.pop_back();
            json.pop_back();
            json += "\""; // آخرین کوتیشن باز را برمی‌گرداند
        }
        json += "}";
        return json;
    }
    
    // **بهبود یافته:** Parse با مدیریت خطای پایه (نیاز به جایگزینی با نوهلمن/جی‌سان در تولید)
    static map<string, string> parse(const string& json_str) {
        map<string, string> data;
        size_t start = json_str.find('{');
        size_t end = json_str.find('}');
        if (start == string::npos || end == string::npos || end <= start) {
            throw runtime_error("Invalid JSON structure.");
        }

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

                if (key.length() > 50 || value.length() > 255) {
                     throw runtime_error("JSON input too large or potentially malicious.");
                }

                if (!key.empty() && !value.empty()) {
                    data[key] = value;
                }
            }
        }
        return data;
    }
};

// ----------------------------------------------------------------------
// --- ۲. کلاس Router (برای ماژولار کردن و رفع نقد نگهداری) ---
// ----------------------------------------------------------------------

using HandlerFunc = function<string(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket)>;

class Router {
private:
    map<string, HandlerFunc> routes;

public:
    void register_route(const string& method, const string& path, HandlerFunc handler) {
        routes[method + " " + path] = handler;
    }

    string route_request(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
        // ۱. جستجوی دقیق برای مسیرهای ثبت شده
        if (routes.count(method + " " + path)) {
            return routes.at(method + " " + path)(method, path, headers, body, client_socket);
        }

        // ۲. هندل کردن مسیرهای فایل ثابت (GET)
        if (method == "GET") {
            // روت اصلی /
            if (path == "/") {
                return build_http_response("<!DOCTYPE html><html><head><title>Minimal Server</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
                    "<h1>فرمانده، خوش آمدید!</h1>"
                    "<p>برای مدیریت فایل‌ها (آپلود/دانلود)، به <a href=\"/files\">/files</a> بروید.</p>"
                    "<p>شمارنده: <a href=\"/count\">/count</a></p>"
                    "<p>تست API (JSON): <a href=\"/api/users\">/api/users</a> (برای ایجاد کاربر جدید از POST استفاده کنید)</p>"
                    "</body></html>", 200);
            } 
            // فایل‌های ثابت و دانلود فایل‌های آپلودی
            else if (path.rfind("/files/", 0) == 0 || path.find('.') != string::npos) { 
                string full_path = (path.rfind("/files/", 0) == 0) ? (UPLOAD_ROOT + path.substr(7)) : (WEB_ROOT + sanitize_path(path));
                serve_static_file(client_socket, full_path);
                return "SERVED"; // نشان می‌دهد که پاسخ مستقیماً به سوکت فرستاده شده است
            }
        }
        
        // ۳. ۴۰۴ برای مسیرهای نامعتبر
        return build_http_response("<h1>404</h1><p>مسیر مورد نظر وجود ندارد.</p>", 404);
    }
};

// ----------------------------------------------------------------------
// --- ۳. توابع کمکی پروتکلی (جزئیات پیاده‌سازی) ---
// ----------------------------------------------------------------------

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
    string status_text = (status_code == 200) ? "OK" : ((status_code == 201) ? "Created" : ((status_code == 404) ? "Not Found" : ((status_code == 400) ? "Bad Request" : "Internal Server Error")));
    
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << content.length() << "\r\n";
    response << "Connection: keep-alive\r\n";
    response << "\r\n";
    response << content;
    
    return response.str();
}

string build_http_response_cacheable(long file_size, const string& content_type) {
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
    vector<char> buffer(BUFFER_SIZE);
    
    while (file.read(buffer.data(), buffer.size())) {
        send(client_socket, buffer.data(), file.gcount(), 0);
    }
    if (file.gcount() > 0) {
        send(client_socket, buffer.data(), file.gcount(), 0);
    }
}

string list_files(const string& upload_dir) {
    string html_content = 
        "<!DOCTYPE html><html><head><title>مدیریت فایل</title></head><body>"
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

void handle_upload_stream(int client_socket, const string& initial_body, long content_length) {
    stringstream ss;
    time_t timer;
    time(&timer);
    ss << UPLOAD_ROOT << "/file_" << std::put_time(std::localtime(&timer), "%Y%m%d%H%M%S") << "_" << rand() % 1000 << ".bin";
    string filename = ss.str();
    
    ofstream outfile(filename, ios::binary);
    if (!outfile.is_open()) {
        string content = "<h1>خطای سرور</h1><p>نمی‌توان فایل را ذخیره کرد.</p>";
        string response_str = build_http_response(content, 500);
        send(client_socket, response_str.c_str(), response_str.length(), 0);
        return;
    }

    outfile.write(initial_body.c_str(), initial_body.length());
    long bytes_written = initial_body.length();
    long remaining_to_read = content_length - bytes_written;
    
    vector<char> buffer(BUFFER_SIZE);
    while (remaining_to_read > 0) {
        size_t read_now = min((long)BUFFER_SIZE, remaining_to_read);
        long bytes_read = read(client_socket, buffer.data(), read_now);
        
        if (bytes_read <= 0) {
            outfile.close();
            remove(filename.c_str()); 
            string content = "<h1>خطای آپلود</h1><p>اتصال قطع شد یا داده ناقص است.</p>";
            string response_str = build_http_response(content, 500);
            send(client_socket, response_str.c_str(), response_str.length(), 0);
            return;
        }
        
        outfile.write(buffer.data(), bytes_read);
        remaining_to_read -= bytes_read;
    }
    
    outfile.close();

    string content = "<h1>آپلود موفقیت‌آمیز</h1><p>فایل با موفقیت در " + filename + " ذخیره شد.</p>";
    string response_str = build_http_response(content, 200);
    send(client_socket, response_str.c_str(), response_str.length(), 0);
    
    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "فایل ذخیره شد: " << filename << endl;
    }
}


// ----------------------------------------------------------------------
// --- ۴. هندلرهای ماژولار (منطق کسب و کار) ---
// ----------------------------------------------------------------------

string api_users_get_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    string all_users_json = "[\n";
    {
        shared_lock<shared_mutex> lock(db_mutex); 
        for (const auto& entry : users_db) {
            all_users_json += JsonParser::stringify(entry.second) + ",\n";
        }
    }
    
    if (users_db.size() > 0) { 
        all_users_json.pop_back(); 
        all_users_json.pop_back(); 
    }
    
    all_users_json += "\n]";
    return build_http_response(all_users_json, 200, "application/json"); 
}

string api_users_post_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    try {
        map<string, string> new_user_data = JsonParser::parse(body);
        
        // **اعتبارسنجی ورودی:** چک کردن فیلدهای ضروری و فرمت ایمیل
        if (new_user_data.count("name") && new_user_data.count("email") && new_user_data.at("email").find('@') != string::npos) {
            
            int id = next_user_id.fetch_add(1);
            new_user_data["id"] = to_string(id);

            {
                unique_lock<shared_mutex> lock(db_mutex); 
                users_db[id] = new_user_data;
            }
            
            string response_json = JsonParser::stringify(new_user_data);

            {
                lock_guard<mutex> lock(cout_mutex);
                cout << "کاربر جدید ایجاد شد: ID " << id << endl;
            }
            return build_http_response(response_json, 201, "application/json"); 

        } else {
            return build_http_response("{\"error\": \"Name and a valid email are required.\"}", 400, "application/json");
        }
    } catch (const exception& e) {
        return build_http_response("{\"error\": \"Invalid JSON format or input: " + string(e.what()) + "\"}", 400, "application/json");
    }
}

string count_get_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    int current_count = ++counter; 
    return build_http_response("<h1>شمارنده</h1><p>صفحه " + to_string(current_count) + " بار بازدید شده است.</p>", 200);
}

string files_get_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    return build_http_response(list_files(UPLOAD_ROOT), 200);
}

string upload_post_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    if (headers.count("content-length")) {
        try {
            long content_length = stol(headers.at("content-length")); 
            handle_upload_stream(client_socket, body, content_length); 
            return "SERVED";
        } catch (const exception& e) {
            return build_http_response("<h1>500</h1><p>خطا در Content-Length یا هنگام ذخیره سازی: " + string(e.what()) + "</p>", 500);
        }
    } else {
         return build_http_response("<h1>400</h1><p>هدر Content-Length لازم است.</p>", 400);
    }
}


// ----------------------------------------------------------------------
// --- ۵. هندلر اصلی کلاینت (ساده و تمیز شده) ---
// ----------------------------------------------------------------------

void handle_client(int client_socket, Router& router) {
    struct timeval timeout;
    timeout.tv_sec = 5; 
    timeout.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);

    vector<char> buffer(BUFFER_SIZE); 
    long valread;
    
    do { 
        valread = read(client_socket, buffer.data(), BUFFER_SIZE - 1); 
        
        if (valread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break; 
        } else if (valread <= 0) {
            break;
        }

        string request(buffer.data(), valread); 
        
        size_t first_line_end = request.find("\r\n");
        size_t headers_end = request.find("\r\n\r\n");

        if (first_line_end == string::npos || headers_end == string::npos) break;

        // پارس کردن خط اول
        string first_line = request.substr(0, first_line_end);
        size_t method_end = first_line.find(' ');
        size_t path_start = method_end + 1;
        size_t path_end = first_line.find(' ', path_start);
        
        if (method_end == string::npos || path_end == string::npos) break;
        
        string method = first_line.substr(0, method_end);
        string full_path_with_query = first_line.substr(path_start, path_end - path_start);
        string path;
        size_t query_pos = full_path_with_query.find('?');
        path = (query_pos != string::npos) ? full_path_with_query.substr(0, query_pos) : full_path_with_query;

        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "درخواست از " << this_thread::get_id() << ": " << method << " " << path << endl;
        }

        // استخراج هدرها و بدنه اولیه
        map<string, string> headers;
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
        string body = request.substr(headers_end + 4); 

        // --- منطق مسیردهی (Routing) ---
        string response_str = router.route_request(method, path, headers, body, client_socket); 

        // ارسال پاسخ 
        if (response_str != "" && response_str != "SERVED") {
            send(client_socket, response_str.c_str(), response_str.length(), 0);
        }
        
    } while (true); // این حلقه تنها زمانی شکسته می‌شود که valread <= 0 باشد یا پاسخ "SERVED" باشد
    
    close(client_socket);
}


// ----------------------------------------------------------------------
// --- ۶. Main Function ---
// ----------------------------------------------------------------------

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    // ۱. ایجاد پوشه آپلود
    if (mkdir(UPLOAD_ROOT.c_str(), 0777) == -1 && errno != EEXIST) {
        perror("mkdir failed for uploads");
        exit(EXIT_FAILURE);
    }
    
    // ۲. تنظیمات سوکت
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
    
    // ۳. پیکربندی Router
    Router router;
    router.register_route("GET", "/api/users", api_users_get_handler);
    router.register_route("POST", "/api/users", api_users_post_handler);
    router.register_route("GET", "/count", count_get_handler);
    router.register_route("GET", "/files", files_get_handler);
    router.register_route("POST", "/upload", upload_post_handler);

    cout << "وب سرور C++ شما (ماژولار و پایدار) در حال اجرا است. پورت: " << PORT << endl;
    
    // ۴. حلقه اصلی Accept
    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue; 
        }
        
        // ایجاد نخ جدید و ارسال Router با ارجاع ثابت
        thread client_thread(handle_client, new_socket, ref(router));
        client_thread.detach(); 
    }
    
    return 0;
}
