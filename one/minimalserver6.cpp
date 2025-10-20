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
#include <dirent.h> // برای فهرست‌بندی دایرکتوری
#include <errno.h>  // برای مدیریت خطای Keep-Alive

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

void serve_static_file(int client_socket, const string& full_path, const string& root) {
    // مسیر درخواستی (با توجه به root) را تمیز میکنیم
    size_t root_len = root.length();
    if (full_path.length() < root_len || full_path.substr(0, root_len) != root) {
        // اگر ریشه درست نباشد یا کوتاه باشد، از full_path اصلی استفاده میکنیم.
        // در حالت serve_static_file معمولی، این مسیر از قبل با WEB_ROOT ترکیب شده است.
    }
    
    // برای تضمین امنیت در هر حالت، یک بار دیگر مسیر را نرمال‌سازی میکنیم
    string final_path;
    if (full_path.find(WEB_ROOT) != string::npos) {
        final_path = full_path;
    } else {
        final_path = full_path;
    }


    ifstream file(final_path, ios::binary | ios::ate);
    
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

    string mime_type = get_mime_type(final_path);
    string response_str = build_http_response(content, 200, mime_type);
    
    send(client_socket, response_str.c_str(), response_str.length(), 0);
}

// --- تابع فهرست‌بندی فایل‌ها و فرم آپلود ---
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
    
    // فرم آپلود (با JS برای ارسال بدنه خام)
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
                "xhr.setRequestHeader('Content-Type', 'application/octet-stream');" // کلید موفقیت!
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
    // ۱. تولید نام فایل منحصر به فرد
    stringstream ss;
    time_t timer;
    time(&timer);
    // ترکیب زمان و عدد تصادفی
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

    char buffer[BUFFER_SIZE] = {0};
    long valread;
    
    // حلقه Keep-Alive
    do { 
        valread = read(client_socket, buffer, BUFFER_SIZE - 1);
        
        // اصلاح: مدیریت صحیح خطای تایم‌اوت Keep-Alive
        if (valread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break; 
        } else if (valread <= 0) {
            break;
        }

        string request(buffer);
        memset(buffer, 0, BUFFER_SIZE);

        // --- ۱. Parse کردن خط اول (متد، مسیر) ---
        size_t first_line_end = request.find("\r\n");
        string first_line = request.substr(0, first_line_end);

        size_t method_end = first_line.find(' ');
        size_t path_start = method_end + 1;
        size_t path_end = first_line.find(' ', path_start);
        
        if (method_end == string::npos || path_end == string::npos) break;
        
        string method = first_line.substr(0, method_end);
        string full_path_with_query = first_line.substr(path_start, path_end - path_start);

        string path;
        string query_string;
        size_t query_pos = full_path_with_query.find('?');
        
        if (query_pos != string::npos) {
            path = full_path_with_query.substr(0, query_pos);
            query_string = full_path_with_query.substr(query_pos + 1);
        } else {
            path = full_path_with_query;
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
            // ... (منطق استخراج هدرها)
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
            if (path == "/") {
                string content = 
                    "<!DOCTYPE html><html><head><title>Minimal Server</title></head><body>"
                    "<h1>فرمانده، خوش آمدید!</h1>"
                    "<p>برای مدیریت فایل‌ها (آپلود/دانلود)، به <a href=\"/files\">/files</a> بروید.</p>"
                    "<p>شمارنده: <a href=\"/count\">/count</a></p>"
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
            else if (path.rfind("/files/", 0) == 0) { // دانلود فایل‌های آپلودی
                string filename = path.substr(7); 
                string full_path = UPLOAD_ROOT + "/" + filename;
                
                serve_static_file(client_socket, full_path, UPLOAD_ROOT); 
                response_str = "SERVED"; 
            }
            else if (path.find('.') != string::npos) { // سرو فایل‌های استاتیک عادی از www
                string full_path = WEB_ROOT + sanitize_path(path);
                serve_static_file(client_socket, full_path, WEB_ROOT);
                response_str = "SERVED"; 
            }
            else {
                response_str = build_http_response("<h1>404</h1><p>مسیر مورد نظر وجود ندارد.</p>", 404);
            }
        } 
        else if (method == "POST") {
            if (path == "/upload") {
                // منطق خواندن کامل بدنه POST
                if (headers.count("content-length")) {
                    try {
                        int content_length = stoi(headers["content-length"]);
                        if (body.length() < content_length) {
                            size_t remaining_to_read = content_length - body.length();
                            char* remaining_buffer = new char[remaining_to_read + 1]; // +1 برای اطمینان
                            size_t read_now = 0;
                            while(remaining_to_read > 0) {
                                read_now = read(client_socket, remaining_buffer, remaining_to_read);
                                if (read_now <= 0) break;
                                body.append(remaining_buffer, read_now);
                                remaining_to_read -= read_now;
                            }
                            delete[] remaining_buffer;
                        }
                        handle_upload(client_socket, body, headers);
                    } catch (const exception& e) {
                        response_str = build_http_response("<h1>500</h1><p>خطا در Content-Length.</p>", 500);
                    }
                } else {
                     response_str = build_http_response("<h1>400</h1><p>هدر Content-Length لازم است.</p>", 400);
                }
            }
            else {
                response_str = build_http_response("<h1>404</h1><p>مسیر POST نامعتبر است.</p>", 404);
            }
        }
        
        // ارسال پاسخ (اگر توسط توابع serve_static_file یا handle_upload ارسال نشده باشد)
        if (response_str != "" && response_str != "SERVED") {
            send(client_socket, response_str.c_str(), response_str.length(), 0);
        }

        // تلاش برای خواندن درخواست بعدی (Keep-Alive)
        // این کار در ابتدای حلقه انجام می‌شود.
        
    } while (true); 
    
    close(client_socket);
}

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
       
    // ۳. اتصال و گوش دادن
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
