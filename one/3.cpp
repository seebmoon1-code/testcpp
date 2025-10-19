#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <iomanip> // برای فرمت‌دهی بهتر جدول

using namespace std;

// === ۱. تعریف نوع داده‌های پایه و تمیزکاری ===

// رنگ‌های ANSI برای خروجی تمیزتر و جذاب‌تر (زیبایی CLI)
namespace Colors {
    const string RESET = "\033[0m";
    const string SUCCESS = "\033[32m"; // سبز
    const string ERROR = "\033[31m";   // قرمز
    const string INFO = "\033[34m";    // آبی
    const string HEADER = "\033[96m";  // فیروزه‌ای روشن
    const string BOLD = "\033[1m";
}

// === ۲. مدیریت ساختار (SRP: مسئولیت مدیریت ستون‌ها) ===

struct Column {
    string name;
    string type; // "STRING" یا "INT"
    // داده‌ها همچنان به عنوان رشته ذخیره می‌شوند، اما هنگام ورود اعتبارسنجی می‌شوند
    vector<string> data; 

    Column(string n, string t = "STRING") : name(std::move(n)) {
        // اطمینان از حروف بزرگ برای نوع داده
        transform(t.begin(), t.end(), t.begin(), ::toupper);
        if (t == "STRING" || t == "INT") {
            type = std::move(t);
        } else {
            type = "STRING";
        }
    }
};

class SchemaManager {
public:
    vector<Column>& columns; // ارجاع به ستون‌های دیتابیس

    // سازنده
    SchemaManager(vector<Column>& cols) : columns(cols) {}

    // ایجاد ساختار (C)
    void createSchema() {
        cout << Colors::INFO << "\n--- ۱. ایجاد ساختار جدید (C) ---" << Colors::RESET << "\n";
        columns.clear();
        cout << "فرمت: [نام] [نوع: STRING/INT]. (برای پایان ENTER بزنید)\n";

        for (int i = 1; ; ++i) {
            cout << "   " << i << ". نام و نوع ستون: ";
            string input_line;
            getline(cin, input_line); 

            if (input_line.empty()) break;

            stringstream ss(input_line);
            string name, type;
            if (!(ss >> name >> type)) {
                // اگر نوع وارد نشد، به صورت پیش‌فرض STRING در نظر گرفته می‌شود
                columns.emplace_back(name);
            } else {
                columns.emplace_back(name, type);
            }
        }
        cout << Colors::SUCCESS << "✅ ساختار با " << columns.size() << " ستون ایجاد شد." << Colors::RESET << "\n";
    }

    // ویرایش ساختار (CA)
    void editSchema() {
        if (columns.empty()) {
            cout << Colors::ERROR << "⚠️ ابتدا با C ساختار را ایجاد کنید." << Colors::RESET << "\n";
            return;
        }
        cout << Colors::INFO << "\n--- ۲. ویرایش ساختار (CA) ---" << Colors::RESET << "\n";

        for (size_t i = 0; i < columns.size(); ++i) {
            cout << i + 1 << ". ستون: " << columns[i].name 
                 << " (نوع: " << columns[i].type << ")\n";
            
            string input_line;
            cout << "   نام/نوع جدید (مثلاً: نام_جدید STRING. ENTER برای حفظ): ";
            getline(cin, input_line);

            if (!input_line.empty()) {
                stringstream ss(input_line);
                string new_name, new_type;
                ss >> new_name >> new_type;

                // اعمال تغییر نام
                if (!new_name.empty()) {
                    columns[i].name = new_name;
                }
                // اعمال تغییر نوع با اعتبارسنجی
                if (!new_type.empty()) {
                    transform(new_type.begin(), new_type.end(), new_type.begin(), ::toupper);
                    if (new_type == "STRING" || new_type == "INT") {
                        columns[i].type = new_type;
                        cout << Colors::SUCCESS << "   نوع به " << new_type << " تغییر یافت." << Colors::RESET << "\n";
                    } else {
                        cout << Colors::ERROR << "   نوع نامعتبر! حفظ نوع قبلی." << Colors::RESET << "\n";
                    }
                }
            }
        }
        cout << Colors::SUCCESS << "✅ ویرایش ساختار به پایان رسید." << Colors::RESET << "\n";
    }

    // نمایش ساختار
    void viewSchema() const {
        if (columns.empty()) {
            cout << Colors::INFO << "⚠️ ساختاری تعریف نشده است." << Colors::RESET << "\n";
            return;
        }
        cout << Colors::HEADER << "\n--- ساختار دیتابیس (Schema) ---" << Colors::RESET << "\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            cout << i + 1 << ". " << columns[i].name << "\t(" << Colors::BOLD << columns[i].type << Colors::RESET << ")\n";
        }
    }
};

// --- ۳. مدیریت داده‌ها (SRP: مسئولیت مدیریت ورودی و اعتبار سنجی) ---

class DataManager {
private:
    vector<Column>& columns;

    // تابع اعتبارسنجی نوع (دقت)
    bool validateData(const string& data, const string& type) {
        if (type == "INT") {
            try {
                size_t pos;
                // تلاش برای تبدیل به int
                (void)stoi(data, &pos); 
                // اطمینان از اینکه تمام رشته مصرف شده و کاراکترهای اضافی ندارد
                return pos == data.length(); 
            } catch (...) {
                return false;
            }
        }
        // STRING همیشه معتبر است
        return true;
    }

public:
    DataManager(vector<Column>& cols) : columns(cols) {}

    // ورود داده (run)
    void runDataEntry() {
        if (columns.empty()) {
            cout << Colors::ERROR << "⚠️ ابتدا ساختار را ایجاد و ویرایش کنید." << Colors::RESET << "\n";
            return;
        }

        cout << Colors::INFO << "\n--- ۳. ورود داده (run) ---" << Colors::RESET << "\n";
        int user_count = columns.front().data.size() + 1;
        
        while (true) {
            cout << Colors::HEADER << "\n-- اطلاعات ردیف #" << user_count << " --" << Colors::RESET << "\n";
            bool should_break = false;
            vector<string> temp_row_data; 

            for (size_t i = 0; i < columns.size(); ++i) {
                string data_input;
                cout << "   " << columns[i].name << " (" << columns[i].type << "): ";
                getline(cin, data_input);

                // پایان ورود در اولین ستون
                if (data_input.empty() && i == 0) {
                    should_break = true;
                    break;
                }
                
                // اعتبارسنجی قوی
                if (!validateData(data_input, columns[i].type)) {
                    cout << Colors::ERROR << "❌ خطای نوع! لطفاً یک " << columns[i].type << " معتبر وارد کنید." << Colors::RESET << "\n";
                    i--; // برگشت به ستون قبلی
                    continue; 
                }
                
                temp_row_data.push_back(data_input);
            }

            if (should_break) {
                cout << Colors::INFO << "پایان ورود داده." << Colors::RESET << "\n";
                break;
            }
            
            // اضافه کردن داده‌های موقت
            for (size_t i = 0; i < columns.size(); ++i) {
                columns[i].data.push_back(temp_row_data[i]);
            }
            user_count++;
        }
    }

    // نمایش داده‌ها (VIEW)
    void viewData() const {
        if (columns.empty() || columns.front().data.empty()) {
            cout << Colors::INFO << "⚠️ دیتابیس خالی است." << Colors::RESET << "\n";
            return;
        }
        
        cout << Colors::HEADER << "\n--- نمایش داده‌های دیتابیس (" << columns.front().data.size() << " ردیف) ---" << Colors::RESET << "\n";
        
        size_t num_rows = columns.front().data.size();
        size_t cell_width = 15; // عرض ثابت برای تمیزی خروجی

        // چاپ هدر
        cout << "|";
        for (const auto& col : columns) {
            cout << Colors::BOLD << Colors::HEADER << left << setw(cell_width) << col.name << Colors::RESET << " |";
        }
        cout << "\n" << string(columns.size() * (cell_width + 2) + 1, '-') << "\n";

        // چاپ سطرها
        for (size_t r = 0; r < num_rows; ++r) {
            cout << "|";
            for (const auto& col : columns) {
                cout << left << setw(cell_width) << col.data[r] << " |";
            }
            cout << "\n";
        }
        cout << string(columns.size() * (cell_width + 2) + 1, '-') << "\n";
    }
};

// --- ۴. مدیریت فایل (SRP: مسئولیت ذخیره و بارگذاری CSV) ---

class FileHandler {
private:
    vector<Column>& columns;

    // توابع کمکی برای تبدیل به CSV و بالعکس
    string to_csv_line(const vector<string>& cells) {
        stringstream ss;
        for (size_t i = 0; i < cells.size(); ++i) {
            ss << "\"" << cells[i] << "\""; 
            if (i < cells.size() - 1) ss << ";";
        }
        return ss.str();
    }

    vector<string> from_csv_line(const string& line) {
        vector<string> cells;
        stringstream ss(line);
        string cell;
        // تجزیه ساده CSV با جداکننده ;
        while (getline(ss, cell, ';')) {
            if (cell.length() >= 2 && cell.front() == '"' && cell.back() == '"') {
                cells.push_back(cell.substr(1, cell.length() - 2));
            } else {
                 cells.push_back(cell);
            }
        }
        return cells;
    }

public:
    FileHandler(vector<Column>& cols) : columns(cols) {}

    void save(const string& filename) {
        if (columns.empty()) {
            cout << Colors::ERROR << "⚠️ دیتابیس خالی است. چیزی برای ذخیره نیست." << Colors::RESET << "\n";
            return;
        }

        ofstream outfile(filename);
        if (!outfile.is_open()) {
            cout << Colors::ERROR << "❌ خطای I/O: نمی‌توان فایل را باز کرد: " << filename << "\n" << Colors::RESET;
            return;
        }

        // خط ۱: هدرها (نام ستون‌ها و نوع)
        vector<string> header;
        for (const auto& col : columns) {
            header.push_back(col.name + " (" + col.type + ")");
        }
        outfile << to_csv_line(header) << "\n";

        // خطوط بعدی: داده‌ها
        size_t num_rows = columns.front().data.size();
        for (size_t r = 0; r < num_rows; ++r) {
            vector<string> row_data;
            for (const auto& col : columns) {
                row_data.push_back(col.data[r]);
            }
            outfile << to_csv_line(row_data) << "\n";
        }

        outfile.close();
        cout << Colors::SUCCESS << "✅ داده‌ها با موفقیت در " << filename << " ذخیره شدند." << Colors::RESET << "\n";
    }

    void load(const string& filename) {
        ifstream infile(filename);
        if (!infile.is_open()) {
            cout << Colors::ERROR << "❌ خطای I/O: فایل پیدا نشد یا قابل باز شدن نیست: " << filename << "\n" << Colors::RESET;
            return;
        }

        string header_line;
        if (!getline(infile, header_line)) {
            cout << Colors::ERROR << "❌ خطای بارگذاری! فایل خالی است." << Colors::RESET << "\n";
            infile.close();
            return;
        }

        // ۱. ساختار (Schema) را از سربرگ می‌خوانیم
        columns.clear();
        vector<string> headers = from_csv_line(header_line);
        for (const string& h : headers) {
            size_t open_paren = h.find('(');
            size_t close_paren = h.find(')');
            string name = h.substr(0, open_paren - 1);
            string type = h.substr(open_paren + 1, close_paren - open_paren - 1);
            columns.emplace_back(name, type);
        }

        // ۲. داده‌ها را می‌خوانیم
        string data_line;
        while (getline(infile, data_line)) {
            vector<string> row_data = from_csv_line(data_line);
            if (row_data.size() != columns.size()) continue; 

            for (size_t i = 0; i < columns.size(); ++i) {
                columns[i].data.push_back(row_data[i]);
            }
        }

        infile.close();
        cout << Colors::SUCCESS << "✅ داده‌ها و ساختار با موفقیت از " << filename << " بارگذاری شدند." << Colors::RESET << "\n";
    }
};

// --- ۵. کلاس اصلی (Engine) و مدیریت دستورات ---

class DatabaseEngine {
private:
    vector<Column> columns;
    SchemaManager schemaManager;
    DataManager dataManager;
    FileHandler fileHandler;

public:
    DatabaseEngine() : 
        schemaManager(columns), 
        dataManager(columns), 
        fileHandler(columns) {}

    void processCommand(const string& command_line) {
        stringstream ss(command_line);
        string main_command;
        ss >> main_command;

        transform(main_command.begin(), main_command.end(), main_command.begin(), ::toupper);

        if (main_command == "C") {
            schemaManager.createSchema();
        } else if (main_command == "CA") {
            schemaManager.editSchema();
        } else if (main_command == "RUN") {
            dataManager.runDataEntry();
        } else if (main_command == "VIEW") {
            dataManager.viewData();
        } else if (main_command == "SCHEMA") {
            schemaManager.viewSchema();
        } else if (main_command == "SAVE") {
            string filename;
            if (ss >> filename) {
                fileHandler.save(filename);
            } else {
                cout << Colors::ERROR << "❌ دستور ناقص! SAVE <نام_فایل.csv>." << Colors::RESET << "\n";
            }
        } else if (main_command == "LOAD") {
            string filename;
            if (ss >> filename) {
                fileHandler.load(filename);
            } else {
                cout << Colors::ERROR << "❌ دستور ناقص! LOAD <نام_فایل.csv>." << Colors::RESET << "\n";
            }
        } else if (main_command == "Q") {
            cout << Colors::INFO << "\n👋 خدا نگهدار. موفق باشید در خلق شاهکارتان!" << Colors::RESET << "\n";
            exit(0); 
        } else {
            cout << Colors::ERROR << "❌ دستور نامعتبر. لطفاً از دستورات منو استفاده کنید." << Colors::RESET << "\n";
        }
    }
};

void display_menu() {
    cout << Colors::BOLD << Colors::HEADER << "\n=================================================" << Colors::RESET << "\n";
    cout << Colors::BOLD << Colors::HEADER << "         ✨ DB CLI: شکوه مهندسی داده ✨" << Colors::RESET << "\n";
    cout << Colors::BOLD << Colors::HEADER << "=================================================" << Colors::RESET << "\n";
    cout << "  " << Colors::BOLD << "C" << Colors::RESET << "       : ایجاد ساختار جدید (Create Schema)\n";
    cout << "  " << Colors::BOLD << "CA" << Colors::RESET << "      : ویرایش ساختار (Alter Schema)\n";
    cout << "  " << Colors::BOLD << "RUN" << Colors::RESET << "     : شروع ورود داده (Add Data)\n";
    cout << "  " << Colors::BOLD << "VIEW" << Colors::RESET << "    : نمایش داده‌ها\n";
    cout << "  " << Colors::BOLD << "SCHEMA" << Colors::RESET << "  : نمایش ساختار فعلی\n";
    cout << "  " << Colors::BOLD << "SAVE" << Colors::RESET << " <file>  : ذخیره‌ی داده‌ها\n";
    cout << "  " << Colors::BOLD << "LOAD" << Colors::RESET << " <file>  : بارگذاری داده‌ها\n";
    cout << "  " << Colors::BOLD << "Q" << Colors::RESET << "       : خروج\n";
    cout << "-------------------------------------------------\n";
    cout << Colors::BOLD << ">> " << Colors::RESET;
}

int main() {
    // تنظیمات اولیه ترمینال برای رنگ و کاراکترهای بین‌المللی
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    DatabaseEngine engine;
    string command_line;

    while (true) {
        display_menu();
        getline(cin, command_line);
        if (command_line.empty()) continue;
        
        try {
            engine.processCommand(command_line);
        } catch (const exception& e) {
            cout << Colors::ERROR << "❌ خطای بحرانی در سیستم: " << e.what() << Colors::RESET << "\n";
        } catch (...) {
            cout << Colors::ERROR << "❌ خطای ناشناخته در هسته‌ی دیتابیس!" << Colors::RESET << "\n";
        }
    }

    return 0;
}
