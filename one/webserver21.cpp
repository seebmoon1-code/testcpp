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
#include <sqlite3.h> 

using namespace std;

// --- تنظیمات ثابت (Const Settings) ---
const int PORT = 8080;
const int BACKLOG = 10;
const int BUFFER_SIZE = 4096;
const string WEB_ROOT = "www";
const string UPLOAD_ROOT = "uploads";
const string DB_PATH = "server_db.sqlite"; 

// --- منابع عمومی و همزمان ---
atomic<int> counter(0); 
mutex cout_mutex; 
mutex db_connection_mutex; 

// --- توابع کمکی پروتکلی (Forward Declarations) ---
string sanitize_path(string path);
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
        size_t depth = 0; 

        for (size_t i = 0; i < content.length(); ++i) {
            if (content[i] == '{' || content[i] == '[') depth++;
            else if (content[i] == '}' || content[i] == ']') depth--;
            else if (content[i] == ',' && depth == 0) {
                segment = trim(content.substr(0, i));
                content = content.substr(i + 1);
                i = 0; 
                
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

                    if (key.length() > 50 || value.length() > 255) {
                        throw runtime_error("JSON input value too large or potentially malicious.");
                    }

                    if (!key.empty() && !value.empty()) {
                        data[key] = value;
                    }
                }
            }
        }
        
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

    static int callback(void* data, int argc, char** argv, char** azColName) {
        auto* result_vec = static_cast<vector<map<string, string>>*>(data);
        map<string, string> row;
        for (int i = 0; i < argc; i++) {
            row[azColName[i]] = argv[i] ? argv[i] : ""; 
        }
        result_vec->push_back(row);
        return 0;
    }
    
    bool execute_select(const string& sql, vector<map<string, string>>& results) {
        char* err_msg = nullptr;
        results.clear();

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
        if (sqlite3_open(db_path.c_str(), &db_ptr) != SQLITE_OK) {
            log_message("خطا در باز کردن دیتابیس: " + string(sqlite3_errmsg(db_ptr)));
            db_ptr = nullptr;
            return false;
        }
        log_message("دیتابیس SQLite3 با موفقیت باز شد.");
        return true;
    }

    bool prepare_and_execute(const string& sql, const vector<string>& params) {
        sqlite3_stmt *stmt;
        lock_guard<mutex> lock(db_connection_mutex);

        if (sqlite3_prepare_v2(db_ptr, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
            log_message("خطا در آماده‌سازی کوئری: " + string(sqlite3_errmsg(db_ptr)) + " | SQL: " + sql);
            return false;
        }

        for (size_t i = 0; i < params.size(); ++i) {
            if (sqlite3_bind_text(stmt, (int)i + 1, params[i].c_str(), (int)params[i].length(), SQLITE_STATIC) != SQLITE_OK) {
                log_message("خطا در اتصال پارامتر " + to_string(i+1) + ": " + string(sqlite3_errmsg(db_ptr)));
                sqlite3_finalize(stmt);
                return false;
            }
        }

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt); 

        if (rc != SQLITE_DONE) { 
            log_message("خطا در اجرای کوئری: " + string(sqlite3_errmsg(db_ptr)));
            return false;
        }
        return true;
    }
    
    bool execute_query(const string& sql, vector<map<string, string>>& results) {
        return execute_select(sql, results);
    }
    
    bool execute_non_query(const string& sql) {
        vector<map<string, string>> dummy_results;
        return execute_select(sql, dummy_results);
    }
    
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
        if (routes.count(method + " " + path)) {
            return routes.at(method + " " + path)(method, path, headers, body, client_socket);
        }
        
        if (method == "DELETE" && path.rfind("/files/", 0) == 0) {
            if (routes.count(method + " " + "/files/")) {
                return routes.at(method + " " + "/files/")(method, path, headers, body, client_socket);
            }
        }
        
        if (method == "PUT" && path.rfind("/api/users/", 0) == 0) {
             if (routes.count(method + " " + "/api/users/")) {
                return routes.at(method + " " + "/api/users/")(method, path, headers, body, client_socket);
            }
        }

        if (method == "GET") {
            if (path == "/") {
                string full_path = WEB_ROOT + "/index.html";
                serve_static_file(client_socket, full_path);
                return "SERVED";
            } 
            else if (path.rfind("/files/", 0) == 0 || path.find('.') != string::npos) { 
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
        
        // پاسخ ۴۰۴ با آرگومان صریح
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
    if (ext == "json") return "application/json";
    
    return "application/octet-stream";
}

void serve_static_file(int client_socket, const string& full_path) { 
    ifstream file(full_path, ios::binary | ios::ate);
    
    if (!file.is_open()) {
        string content = "<h1>404 - پیدا نشد</h1><p>فایل یا مسیر در سرور پیدا نشد.</p>";
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
    string html_content =
        "<!DOCTYPE html><html><head><title>مدیریت فایل</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body class=\"list-files-section\">"
        "<h1>مدیریت فایل‌ها (فایل منیجر)</h1>"
        "<p>فایل‌های آپلود شده در پوشه‌ی <code>uploads/</code> ذخیره می‌شوند. این فرآیند از <a href=\"/\" style=\"color:#3498db;\">Streaming Upload</a> استفاده می‌کند.</p>"
        
        "<h2>✅ آپلود فایل جدید</h2>"
        "<div class=\"upload-box\">"
        "<input type=\"file\" id=\"fileInput\" required>"
        "<button onclick=\"uploadFile()\">آپلود فایل</button>"
        "<span id=\"uploadStatus\" style=\"margin-right: 15px; font-weight: bold;\"></span>"
        "</div>"
        
        "<h2>📂 فایل‌های ذخیره شده</h2><ul>";

    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(upload_dir.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            string filename = ent->d_name;
            if (filename != "." && filename != "..") {
                html_content += "<li>"
                                "<a href=\"/files/" + filename + "\" target=\"_blank\">" + filename + "</a>"
                                "<button onclick=\"deleteFile('" + filename + "')\">حذف دائمی</button>"
                                "</li>";
            }
        }
        closedir(dir);
    } else {
        html_content += "<li>خطا: قادر به باز کردن پوشه‌ی آپلود نیست. مطمئن شوید پوشه <code>uploads/</code> وجود دارد.</li>";
    }

    html_content += "</ul>";
    
    html_content +=
        "<script>"
        "function uploadFile() {"
            "const fileInput = document.getElementById('fileInput');"
            "const statusSpan = document.getElementById('uploadStatus');"
            "const file = fileInput.files[0];"
            "if (!file) { alert('لطفا یک فایل انتخاب کنید.'); return; }"
            
            "statusSpan.textContent = 'در حال آپلود...'; statusSpan.style.color = '#e67e22';"
            "const xhr = new XMLHttpRequest();"
            "xhr.open('POST', '/upload', true);"
            "xhr.setRequestHeader('Content-Type', 'application/octet-stream');" 
            
            "xhr.onload = function() {"
                "let response = {error: 'خطای نامشخص'};"
                "try { response = JSON.parse(xhr.responseText); } catch(e) {}"
                
                "if (xhr.status === 200) {"
                    "statusSpan.textContent = '✅ آپلود موفقیت‌آمیز';"
                    "setTimeout(() => { window.location.reload(); }, 1500);" 
                "} else {"
                    "statusSpan.textContent = '❌ خطای آپلود';"
                    "alert('خطا در آپلود: ' + (response.error || 'Server Error.'));"
                "}"
            "};"
            
            "xhr.onerror = function() {"
                "statusSpan.textContent = '❌ خطای شبکه';"
                "alert('خطای شبکه رخ داد.');"
            "};"
            
            "xhr.send(file);" 
        "}"
        
        "function deleteFile(filename) {"
            "if (confirm('⚠️ آیا مطمئن هستید که می‌خواهید ' + filename + ' را حذف کنید؟ این عملیات برگشت ناپذیر است.')) {"
                "const xhr = new XMLHttpRequest();"
                "xhr.open('DELETE', '/files/' + filename, true);"
                
                "xhr.onload = function() {"
                    "const response = JSON.parse(xhr.responseText);"
                    "if (xhr.status === 200) {"
                        "alert('✅ حذف موفقیت‌آمیز: ' + response.message);"
                        "window.location.reload();"
                    "} else {"
                        "alert('❌ خطا در حذف فایل: ' + response.error);"
                    "}"
                "};"
                "xhr.send();"
            "}"
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
        log_message("خطا در باز کردن فایل برای ذخیره: " + filename);
        string response_str = build_http_response("{\"error\": \"Cannot save file on server disk.\"}", 500, "application/json");
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
            log_message("قطع اتصال یا داده ناقص هنگام آپلود.");
            string response_str = build_http_response("{\"error\": \"Connection lost or incomplete data during upload.\"}", 500, "application/json");
            send(client_socket, response_str.c_str(), response_str.length(), 0);
            return;
        }
        
        outfile.write(buffer.data(), bytes_read);
        remaining_to_read -= bytes_read;
    }
    
    outfile.close();

    log_message("فایل ذخیره شد: " + filename); 
    string response_json = "{\"message\": \"File uploaded successfully to " + filename + "\", \"path\": \"/files/" + filename.substr(UPLOAD_ROOT.length() + 1) + "\"}";
    string response_str = build_http_response(response_json, 200, "application/json");
    send(client_socket, response_str.c_str(), response_str.length(), 0);
}


// ----------------------------------------------------------------------
// --- ۴. هندلرهای ماژولار (CRUD) ---
// ----------------------------------------------------------------------

// R - Read All Users
string api_users_get_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    vector<map<string, string>> users;
    
    if (!db_manager->execute_query("SELECT id, name, email FROM users;", users)) {
        return build_http_response("{\"error\": \"Failed to retrieve users from database.\"}", 500, "application/json");
    }

    string all_users_json = "[\n";
    for (size_t i = 0; i < users.size(); ++i) {
        all_users_json += JsonParser::stringify(users[i]);
        if (i < users.size() - 1) {
            all_users_json += ",\n";
        }
    }
    all_users_json += "\n]";
    
    return build_http_response(all_users_json, 200, "application/json"); 
}

// C - Create New User
string api_users_post_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    try {
        map<string, string> new_user_data = JsonParser::parse(body);
        
        if (new_user_data.count("name") && new_user_data.count("email") && new_user_data.at("email").find('@') != string::npos) {
            
            string name = new_user_data.at("name");
            string email = new_user_data.at("email");
            
            string sql = "INSERT INTO users (name, email) VALUES (?, ?);";
            vector<string> params = {name, email};
            
            if (db_manager->prepare_and_execute(sql, params)) {
                
                long new_id = db_manager->get_last_insert_id();

                new_user_data["id"] = to_string(new_id);
                string response_json = JsonParser::stringify(new_user_data);

                log_message("کاربر جدید در دیتابیس ایجاد شد: ID " + new_user_data.at("id"));
                return build_http_response(response_json, 201, "application/json");

            } else {
                return build_http_response("{\"error\": \"Database insertion failed (Email might be duplicate, UNIQUE constraint violation).\"}", 500, "application/json");
            }
        } else {
            return build_http_response("{\"error\": \"Name and a valid email are required.\"}", 400, "application/json");
        }
    } catch (const exception& e) {
        // خط ۶۴۲: اصلاح نقل‌قول پایانی رشته
        return build_http_response("{\"error\": \"Invalid JSON format or input: " + string(e.what()) + "\"}", 400, "application/json");
    }
}

// U - Update Existing User
string api_users_put_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    try {
        size_t id_start = path.find_last_of('/') + 1;
        if (id_start == 0 || id_start >= path.length()) {
            return build_http_response("{\"error\": \"User ID is missing from URL.\"}", 400, "application/json");
        }
        string id_str = path.substr(id_start);
        
        map<string, string> update_data = JsonParser::parse(body);
        
        if (!(update_data.count("name") || update_data.count("email"))) {
            return build_http_response("{\"error\": \"Require 'name' or 'email' field to update.\"}", 400, "application/json");
        }
        
        string sql = "UPDATE users SET ";
        vector<string> params;
        
        if (update_data.count("name")) {
            sql += "name = ?, ";
            params.push_back(update_data.at("name"));
        }
        if (update_data.count("email")) {
            sql += "email = ?, ";
            params.push_back(update_data.at("email"));
        }
        
        sql = sql.substr(0, sql.length() - 2); 
        sql += " WHERE id = ?;";
        params.push_back(id_str);

        if (db_manager->prepare_and_execute(sql, params)) {
            log_message("کاربر با ID " + id_str + " به‌روزرسانی شد.");
            return build_http_response("{\"message\": \"User " + id_str + " updated successfully.\"}", 200, "application/json");
        } else {
            return build_http_response("{\"error\": \"Database update failed (ID not found or Email is duplicate).\"}", 500, "application/json");
        }

    } catch (const exception& e) {
        // خط ۶۸۸: اصلاح نقل‌قول پایانی رشته
        return build_http_response("{\"error\": \"Invalid JSON format or URL structure: " + string(e.what()) + "\"}", 400, "application/json");
    }
}

// Handler برای شمارنده (تست Atomic)
string count_get_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    int current_count = ++counter; 
    return build_http_response("<h1>شمارنده</h1><p>صفحه " + to_string(current_count) + " بار بازدید شده است.</p>", 200, "text/html");
}

// Handler برای صفحه File Manager
string files_get_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    return build_http_response(list_files(UPLOAD_ROOT), 200, "text/html");
}

// Handler برای دریافت فایل آپلودی (Streaming)
string upload_post_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    if (headers.count("content-length")) {
        try {
            long content_length = stol(headers.at("content-length")); 
            if (content_length > 1024 * 1024 * 500) { 
                 return build_http_response("{\"error\": \"File size exceeds 500MB limit.\"}", 413, "application/json");
            }
            handle_upload_stream(client_socket, body, content_length); 
            return "SERVED";
        } catch (const exception& e) {
            return build_http_response("{\"error\": \"Error processing Content-Length or during streaming: " + string(e.what()) + "\"}", 500, "application/json");
        }
    } else {
        return build_http_response("{\"error\": \"Content-Length header is required for upload.\"}", 400, "application/json");
    }
}

// D - Delete File
string files_delete_handler(const string& method, const string& path, const map<string, string>& headers, const string& body, int client_socket) {
    if (path.length() <= 7) {
        return build_http_response("{\"error\": \"Filename is missing.\"}", 400, "application/json");
    }

    string filename_to_delete = path.substr(7);
    string full_path = UPLOAD_ROOT + "/" + filename_to_delete;

    if (filename_to_delete.find("..") != string::npos || filename_to_delete.find('/') != string::npos) {
        return build_http_response("{\"error\": \"Security check failed (Invalid characters or Directory Traversal detected).\"}", 403, "application/json");
    }

    if (remove(full_path.c_str()) != 0) {
        if (errno == ENOENT) {
            return build_http_response("{\"error\": \"File not found.\"}", 404, "application/json");
        } else {
            log_message("خطا در حذف فایل: " + full_path + " - Error: " + strerror(errno));
            return build_http_response("{\"error\": \"Could not delete file due to server error.\"}", 500, "application/json");
        }
    }

    log_message("فایل حذف شد: " + full_path);
    return build_http_response("{\"message\": \"File deleted successfully.\"}", 200, "application/json");
}


// ----------------------------------------------------------------------
// --- ۵. هندلر اصلی کلاینت (ارتباط سوکت) ---
// ----------------------------------------------------------------------

void handle_client(int client_socket, Router& router) {
    struct timeval timeout;
    timeout.tv_sec = 5; 
    timeout.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);

    vector<char> buffer(BUFFER_SIZE); 
    long valread;
    
    do { 
        fill(buffer.begin(), buffer.end(), 0);
        valread = read(client_socket, buffer.data(), BUFFER_SIZE - 1); 
        
        if (valread <= 0) {
            break; 
        }

        string request(buffer.data(), valread); 
        
        size_t first_line_end = request.find("\r\n");
        size_t headers_end = request.find("\r\n\r\n");

        if (first_line_end == string::npos || headers_end == string::npos) break;

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

        log_message("درخواست: " + method + " " + path); 

        map<string, string> headers;
        string headers_raw = request.substr(first_line_end + 2, headers_end - first_line_end - 2);
        stringstream header_ss(headers_raw);
        string line;
        
        while (getline(header_ss, line, '\r')) {
            if (line.empty() || line == "\n") continue;
            size_t colon = line.find(':');
            if (colon != string::npos) {
                string key = line.substr(0, colon);
                string value = line.substr(colon + 1);
                
                transform(key.begin(), key.end(), key.begin(), ::tolower);
                key = JsonParser::trim(key);
                value = JsonParser::trim(value);
                
                if (!key.empty()) {
                    headers[key] = value;
                }
            }
        }
        
        string body = request.substr(headers_end + 4); 
        long content_length = 0;
        
        if (headers.count("content-length")) {
            try {
                content_length = stol(headers.at("content-length"));
            } catch(...) {
            }
        }

        if (body.length() < content_length) {
            long remaining = content_length - body.length();
            
            while (remaining > 0) {
                size_t read_now = min((long)BUFFER_SIZE, remaining);
                
                fill(buffer.begin(), buffer.end(), 0);
                long bytes_read = read(client_socket, buffer.data(), read_now);
                
                if (bytes_read <= 0) {
                    log_message("قطع اتصال یا داده ناقص هنگام خواندن بدنه.");
                    break;
                }
                body.append(buffer.data(), bytes_read);
                remaining -= bytes_read;
            }
        }

        string response_str = router.route_request(method, path, headers, body, client_socket);
        
        if (response_str != "SERVED") {
            send(client_socket, response_str.c_str(), response_str.length(), 0);
        }
        
        if (headers.count("connection") && headers.at("connection") == "close") {
            break; 
        }

    } while (true);
    
    close(client_socket);
    log_message("اتصال کلاینت بسته شد.");
}

// ----------------------------------------------------------------------
// --- ۶. توابع پیکربندی و Main ---
// ----------------------------------------------------------------------

bool setup_directories() {
    if (mkdir(WEB_ROOT.c_str(), 0777) == -1 && errno != EEXIST) {
        log_message("خطا در ایجاد پوشه WEB_ROOT: " + string(strerror(errno)));
        return false;
    }
    if (mkdir(UPLOAD_ROOT.c_str(), 0777) == -1 && errno != EEXIST) {
        log_message("خطا در ایجاد پوشه UPLOAD_ROOT: " + string(strerror(errno)));
        return false;
    }
    log_message("پوشه‌های اصلی ایجاد شدند (WEB_ROOT و UPLOAD_ROOT).");
    return true;
}

bool setup_database() {
    db_manager = make_unique<DatabaseManager>();
    if (!db_manager->open(DB_PATH)) {
        log_message("عدم موفقیت در اتصال به دیتابیس.");
        return false;
    }

    string create_table_sql = 
        "CREATE TABLE IF NOT EXISTS users("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "email TEXT NOT NULL UNIQUE"
        ");";
        
    if (!db_manager->execute_non_query(create_table_sql)) {
        log_message("خطا در ایجاد جدول users.");
        return false;
    }
    log_message("جدول users با موفقیت آماده شد.");
    return true;
}

void setup_routes(Router& router) {
    router.register_route("GET", "/api/users", api_users_get_handler);    
    router.register_route("POST", "/api/users", api_users_post_handler);  
    router.register_route("PUT", "/api/users/", api_users_put_handler);   

    router.register_route("GET", "/count", count_get_handler);             
    router.register_route("GET", "/files", files_get_handler);             
    router.register_route("POST", "/upload", upload_post_handler);          
    router.register_route("DELETE", "/files/", files_delete_handler);       
    
    log_message("مسیرهای وب سرور پیکربندی شدند.");
}

int main() {
    log_message("شروع وب سرور C++...");
    
    if (!setup_directories()) return 1;
    if (!setup_database()) return 1;
    
    Router router;
    setup_routes(router);

    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("خطا در ساخت سوکت");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("خطا در setsockopt");
        return 1;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("خطا در bind");
        return 1;
    }
    
    if (listen(server_fd, BACKLOG) < 0) {
        perror("خطا در listen");
        return 1;
    }
    
    log_message("سرور در حال گوش دادن روی پورت " + to_string(PORT) + " است...");
    
    while(true) {
        log_message("منتظر اتصال جدید...");
        
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            if (errno == EINTR) continue; 
            perror("خطا در accept");
            continue;
        }
        
        thread client_thread(handle_client, new_socket, ref(router));
        client_thread.detach(); 
    }

    close(server_fd);
    log_message("سرور خاموش شد.");
    return 0;
}
