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
#include <functional>
#include <sqlite3.h> // کتابخانه SQLite3

using namespace std;

// --- تنظیمات ثابت (Const Settings) ---
const int PORT = 8080;
const int BACKLOG = 10;
const int BUFFER_SIZE = 4096;
const string WEB_ROOT = "www";
const string UPLOAD_ROOT = "uploads";
const string DB_PATH = "server_db.sqlite"; // مسیر دیتابیس

// --- منابع عمومی و همزمان ---
atomic<int> counter(0); // شمارنده اتمیک برای تست Multi-thread
mutex cout_mutex; // قفل برای لاگ‌گیری ایمن
mutex db_connection_mutex; // قفل برای دسترسی به شیء اتصال SQLite

// --- توابع کمکی پروتکلی (Forward Declarations) ---
string sanitize_path(string path);
// تغییر در اینجا: مقدار پیش‌فرض را حفظ می‌کنیم، اما در فراخوانی‌ها صریح می‌شویم.
string build_http_response(const string& content, int status_code, const string& content_type = "text/html");
string build_http_response_cacheable(long file_size, const string& content_type);
string get_mime_type(const string& file_path);
void serve_static_file(int client_socket, const string& full_path);
string list_files(const string& upload_dir);
void handle_upload_stream(int client_socket, const string& initial_body, long content_length);

// ----------------------------------------------------------------------
// --- تابع کمکی لاگ‌گیری (تمیز کردن خروجی کنسول) ---
// ----------------------------------------------------------------------

void log_message(const string& message) {
    lock_guard<mutex> lock(cout_mutex);
    time_t now = time(0);
    struct tm* ltm = localtime(&now);
    cout << "[" << put_time(ltm, "%H:%M:%S") << " T" << this_thread::get_id() << "] " << message << endl;
}

// ----------------------------------------------------------------------
// --- ۱. ابزارهای JSON ---
// ----------------------------------------------------------------------

class JsonParser {
public:
    static string stringify(const map<string, string>& data) {
        string json = "{";
        for (const auto& pair : data) {
            // فرار از نقل قول‌های احتمالی در مقادیر
            string safe_value = pair.second;
            size_t pos = 0;
            while ((pos = safe_value.find('\"', pos)) != string::npos) { safe_value.replace(pos, 1, "\\\""); pos += 2; }
            
            json += "\"" + pair.first + "\": \"" + safe_value + "\",";
        }
        if (json.length() > 1) { 
            json.pop_back();
        }
        json += "}";
        return json;
    }
    
    static string trim(const string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (string::npos == first) return "";
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, (last - first + 1));
    }

    static map<string, string> parse(const string& json_str) {
        map<string, string> data;
        size_t start = json_str.find('{');
        size_t end = json_str.find_last_of('}');
        if (start == string::npos || end == string::npos || end <= start) {
            throw runtime_error("Invalid JSON structure (missing brackets).");
        }

        string content = json_str.substr(start + 1, end - start - 1);
        string segment;
        size_t depth = 0; // برای مدیریت آرایه‌ها یا آبجکت‌های تودرتو (اولیه)

        // تجزیه بخش‌های key:value بر اساس ویرگول (,)
        for (size_t i = 0; i < content.length(); ++i) {
            if (content[i] == '{' || content[i] == '[') depth++;
            else if (content[i] == '}' || content[i] == ']') depth--;
            else if (content[i] == ',' && depth == 0) {
                // اگر ویرگول جداکننده باشد
                segment = trim(content.substr(0, i));
                content = content.substr(i + 1);
                i = 0; // شروع مجدد از ابتدای محتوای باقیمانده
                
                size_t colon = segment.find(':');
                if (colon != string::npos) {
                    string key = segment.substr(0, colon);
                    string value = segment.substr(colon + 1);
                    
                    key = trim(key);
                    value = trim(value);

                    // حذف نقل قول‌های اطراف (اگر وجود داشته باشند)
                    if (key.length() > 0 && key.front() == '\"' && key.back() == '\"') key = key.substr(1, key.length() - 2);
                    if (value.length() > 0 && value.front() == '\"' && value.back() == '\"') value = value.substr(1, value.length() - 2);

                    key = trim(key);
                    value = trim(value);

                    if (key.length() > 50 || value.length() > 255) {
                        throw runtime_error("JSON input value too large or potentially malicious.");
                    }

                    if (!key.empty() && !value.empty()) {
                        data[key] = value;
                    }
                }
            }
        }
        
        // پردازش آخرین سگمنت
        segment = trim(content);
        if (!segment.empty()) {
             size_t colon = segment.find(':');
             if (colon != string::npos) {
                 string key = segment.substr(0, colon);
                 string value = segment.substr(colon + 1);
                 
                 key = trim(key);
                 value = trim(value);

                 if (key.length() > 0 && key.front() == '\"' && key.back() == '\"') key = key.substr(1, key.length() - 2);
                 if (value.length() > 0 && value.front() == '\"' && value.back() == '\"') value = value.substr(1, value.length() - 2);

                 key = trim(key);
                 value = trim(value);
                 
                 if (!key.empty() && !value.empty()) {
                     data[key] = value;
                 }
             }
        }
        
        if (data.empty() && trim(json_str).length() > 2) {
            throw runtime_error("JSON input is not a simple key-value map.");
        }
        
        return data;
    }
};

// ----------------------------------------------------------------------
// --- ۱.۵. کلاس DatabaseManager (SQLite3 - با Prepared Statements امن) ---
// ----------------------------------------------------------------------

class DatabaseManager {
private:
    sqlite3* db_ptr;

    // تابع کال‌بک برای واکشی نتایج
    static int callback(void* data, int argc, char** argv, char** azColName) {
        auto* result_vec = static_cast<vector<map<string, string>>*>(data);
        map<string, string> row;
        for (int i = 0; i < argc; i++) {
            row[azColName[i]] = argv[i] ? argv[i] : ""; // NULL به رشته خالی تبدیل می‌شود
        }
        result_vec->push_back(row);
        return 0;
    }
    
    // تابع کمکی عمومی برای کوئری‌های SELECT (که نیاز به نتایج دارند)
    bool execute_select(const string& sql, vector<map<string, string>>& results) {
        char* err_msg = nullptr;
        results.clear();

        // قفل کردن دسترسی به شیء اتصال (برای ایمنی Thread)
        lock_guard<mutex> lock(db_connection_mutex); 

        int rc = sqlite3_exec(db_ptr, sql.c_str(), callback, &results, &err_msg);
        
        if (rc != SQLITE_OK) {
            log_message("خطا در کوئری SELECT: " + string(err_msg) + " | SQL: " + sql);
            sqlite3_free(err_msg);
            return false;
        }
        return true;
    }

public:
    DatabaseManager() : db_ptr(nullptr) {}
    ~DatabaseManager() {
        if (db_ptr) {
            sqlite3_close(db_ptr);
        }
    }

    bool open(const string& db_path) {
        // باز کردن دیتابیس در حالت Serialized (امن برای Multi-Thread)
        if (sqlite3_open(db_path.c_str(), &db_ptr) != SQLITE_OK) {
            log_message("خطا در باز کردن دیتابیس: " + string(sqlite3_errmsg(db_ptr)));
            db_ptr = nullptr;
            return false;
        }
        log_message("دیتابیس SQLite3 با موفقیت باز شد.");
        return true;
    }

    // متد اصلی برای اجرای INSERT, UPDATE, DELETE (استفاده از Prepared Statements)
    bool prepare_and_execute(const string& sql, const vector<string>& params) {
        sqlite3_stmt *stmt;
        lock_guard<mutex> lock(db_connection_mutex);

        // ۱. آماده‌سازی (Prepare)
        if (sqlite3_prepare_v2(db_ptr, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
            log_message("خطا در آماده‌سازی کوئری: " + string(sqlite3_errmsg(db_ptr)) + " | SQL: " + sql);
            return false;
        }

        // ۲. اتصال پارامترها (Bind) - پارامترها از ۱ شروع می‌شوند.
        for (size_t i = 0; i < params.size(); ++i) {
            if (sqlite3_bind_text(stmt, (int)i + 1, params[i].c_str(), (int)params[i].length(), SQLITE_STATIC) != SQLITE_OK) {
                log_message("خطا در اتصال پارامتر " + to_string(i+1) + ": " + string(sqlite3_errmsg(db_ptr)));
                sqlite3_finalize(stmt);
                return false;
            }
        }

        // ۳. اجرا (Step)
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt); // پاکسازی منابع

        if (rc != SQLITE_DONE) { // SQLITE_DONE برای INSERT, UPDATE, DELETE موفق است
            log_message("خطا در اجرای کوئری: " + string(sqlite3_errmsg(db_ptr)));
            return false;
        }
        return true;
    }
    
    // متد دسترسی به SELECT
    bool execute_query(const string& sql, vector<map<string, string>>& results) {
        return execute_select(sql, results);
    }
    
    // متد دسترسی به کوئری‌های بدون نتیجه (فقط برای CREATE TABLE)
    bool execute_non_query(const string& sql) {
        vector<map<string, string>> dummy_results;
        return execute_select(sql, dummy_results);
    }
    
    // متد کمکی برای دریافت آخرین ID
    long get_last_insert_id() {
        lock_guard<mutex> lock(db_connection_mutex);
        return sqlite3_last_insert_rowid(db_ptr);
    }
};

unique_ptr<DatabaseManager> db_manager;

// ----------------------------------------------------------------------
// --- ۲. کلاس Router و توابع کمکی پروتکلی ---
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
        // ۱. بررسی مسیرهای مستقیم
        if (routes.count(method + " " + path)) {
            return routes.at(method + " " + path)(method, path, headers, body, client_socket);
        }
        
        // ۲. بررسی مسیرهای با Wildcard (مانند DELETE /files/filename یا PUT /api/users/5)
        
        // Wildcard: DELETE /files/
        if (method == "DELETE" && path.rfind("/files/", 0) == 0) {
            if (routes.count(method + " " + "/files/")) {
                return routes.at(method + " " + "/files/")(method, path, headers, body, client_socket);
            }
        }
        
        // Wildcard: PUT /api/users/
        if (method == "PUT" && path.rfind("/api/users/", 0) == 0) {
             if (routes.count(method + " " + "/api/users/")) {
                return routes.at(method + " " + "/api/users/")(method, path, headers, body, client_socket);
            }
        }

        if (method == "GET") {
            if (path == "/") {
                // صفحه اصلی HTML کامل از فایل index.html (که در Router نیست)
                string full_path = WEB_ROOT + "/index.html";
                serve_static_file(client_socket, full_path);
                return "SERVED";
            } 
            else if (path.rfind("/files/", 0) == 0 || path.find('.') != string::npos) { 
                // سرویس‌دهی فایل‌های استاتیک (مثل css و js) و فایل‌های آپلودی
                string full_path;
                if (path.rfind("/files/", 0) == 0) {
                    full_path = UPLOAD_ROOT + sanitize_path(path).substr(7);
                } else {
                    full_path = WEB_ROOT + sanitize_path(path);
                }
                serve_static_file(client_socket, full_path);
                return "SERVED"; 
            }
        }
        
        // **اصلاح نهایی ارور کامپایلری در این خط:**
        // پاسخ ۴۰۴ - آرگومان سوم به صورت صریح اضافه شد
        return build_http_response("<h1>404</h1><p>مسیر مورد نظر وجود ندارد.</p>", 404, "text/html");
    }
};

// --- ۳. توابع کمکی پروتکلی (پروتکل و I/O) ---
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

string build_http_response(const string& content, int status_code, const string& content_type) {
    stringstream response;
    string status_text;
    
    // تخصیص متن وضعیت بر اساس کد
    if (status_code == 200) status_text = "OK";
    else if (status_code == 201) status_text = "Created";
    else if (status_code == 400) status_text = "Bad Request";
    else if (status_code == 403) status_text = "Forbidden";
    else if (status_code == 404) status_text = "Not Found";
    else if (status_code == 413) status_text = "Payload Too Large";
    else if (status_code == 500) status_text = "Internal Server Error";
    else status_text = "Unknown";
    
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
    response << "Cache-Control: public, max-age=604800\r\n"; // کش کردن برای یک هفته
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
    if (ext == "json") return "application/json";
    
    return "application/octet-stream";
}

void serve_static_file(int client_socket, const string& full_path) { 
    // تابع برای ارسال فایل‌های استاتیک یا آپلودی
    ifstream file(full_path, ios::binary | ios::ate);
    
    if (!file.is_open()) {
        string content = "<h1>404 - پیدا نشد</h1><p>فایل یا مسیر در سرور پیدا نشد.</p>";
        // **اصلاح صریح آرگومان سوم:**
        string response_str = build_http_response(content, 404, "text/html");
        send(client_socket, response_str.c_str(), response_str.length(), 0);
        return;
    }
    
    long file_size = file.tellg();
    file.seekg(0, ios::beg);

    string mime_type = get_mime_type(full_path);
    
    string response_headers = build_http_response_cacheable(file_size, mime_type);
    send(client_socket, response_headers.c_str(), response_headers.length(), 0);
    
    vector<char> buffer(BUFFER_SIZE);
    
    while (file.read(buffer.data(), buffer.size())) {
        send(client_socket, buffer.data(), file.gcount(), 0);
    }
    if (file.gcount() > 0) {
        send(client_socket, buffer.data(), file.gcount(), 0);
    }
}

string list_files(const string& upload_dir) {
    // تابع ساخت صفحه HTML مدیریت فایل (File Manager)
    string
