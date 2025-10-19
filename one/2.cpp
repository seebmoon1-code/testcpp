#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace std;

// === ساختارهای اصلی ===

// ساختار اصلی یک ستون
struct Column {
    string name;
    string type; // "STRING" یا "INT"
    // داده‌های ستون به صورت رشته‌ای ذخیره می‌شوند
    vector<string> data; 

    // سازنده
    Column(string n, string t = "STRING") : name(std::move(n)), type(std::move(t)) {}
};

// === کلاس دیتابیس ===

class SimpleDB {
private:
    vector<Column> columns;

    // یک تابع کمکی برای تبدیل برداری از رشته‌ها به یک خط CSV
    string to_csv_line(const vector<string>& cells) {
        stringstream ss;
        for (size_t i = 0; i < cells.size(); ++i) {
            // برای سادگی، جداکننده از ; استفاده شده تا با کاماهای داخل داده تداخل نکند
            ss << "\"" << cells[i] << "\""; 
            if (i < cells.size() - 1) {
                ss << ";";
            }
        }
        return ss.str();
    }

    // یک تابع کمکی برای تجزیه یک خط CSV
    vector<string> from_csv_line(const string& line) {
        vector<string> cells;
        stringstream ss(line);
        string cell;
        // این یک تجزیه کننده CSV ساده است
        while (getline(ss, cell, ';')) {
            // حذف علامت نقل قول " از ابتدا و انتها
            if (cell.length() >= 2 && cell.front() == '"' && cell.back() == '"') {
                cells.push_back(cell.substr(1, cell.length() - 2));
            } else {
                 cells.push_back(cell); // در صورت نبود نقل قول
            }
        }
        return cells;
    }

public:
    SimpleDB() = default;

    // --- ۱. ایجاد ساختار (C) ---
    void create_schema() {
        cout << "\n--- ۱. ایجاد ساختار (C) ---\n";
        columns.clear(); // پاک کردن ساختار قبلی
        string col_name;
        int col_index = 1;

        cout << "دستور: [نام] [نوع: STRING/INT]. (برای پایان ENTER بزنید)\n";
        while (true) {
            cout << "   " << col_index << ". نام ستون و نوع آن: ";
            getline(cin, col_name); 

            if (col_name.empty()) {
                break;
            }

            // جداسازی نام و نوع (مثلاً "نام STRING" یا "سن INT")
            stringstream ss(col_name);
            string name, type = "STRING"; // پیش فرض STRING
            ss >> name >> type;

            // تبدیل نوع به حروف بزرگ
            transform(type.begin(), type.end(), type.begin(), ::toupper);
            if (type != "STRING" && type != "INT") {
                type = "STRING"; // اگر نامعتبر بود، پیش فرض اعمال شود
            }

            columns.emplace_back(name, type);
            col_index++;
        }
        cout << "✅ ساختار اولیه با " << columns.size() << " ستون ایجاد شد.\n";
    }

    // --- ۲. ویرایش ساختار (CA) ---
    void edit_schema() {
        if (columns.empty()) {
            cout << "⚠️ ابتدا با C ساختار را ایجاد کنید.\n";
            return;
        }

        cout << "\n--- ۲. ویرایش ساختار (CA) ---\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            cout << i + 1 << ". ستون فعلی: " << columns[i].name 
                 << " (نوع: " << columns[i].type << ")\n";
            
            string input_line;
            cout << "   نام/نوع جدید (مثلاً: نام_جدید STRING. ENTER برای حفظ): ";
            getline(cin, input_line);

            if (!input_line.empty()) {
                stringstream ss(input_line);
                string new_name, new_type;
                ss >> new_name >> new_type;

                if (!new_name.empty()) {
                    columns[i].name = new_name;
                }
                if (!new_type.empty()) {
                    transform(new_type.begin(), new_type.end(), new_type.begin(), ::toupper);
                    if (new_type == "STRING" || new_type == "INT") {
                        columns[i].type = new_type;
                    }
                }
            }
        }
        cout << "✅ ویرایش ساختار به پایان رسید.\n";
    }

    // --- ۳. ورود داده (run) ---
    void run_data_entry() {
        if (columns.empty()) {
            cout << "⚠️ ابتدا ساختار را ایجاد و ویرایش کنید.\n";
            return;
        }

        cout << "\n--- ۳. ورود داده (run) ---\n";
        int user_count = 1;
        
        while (true) {
            cout << "\n-- اطلاعات ردیف " << user_count << " --\n";
            bool should_break = false;
            
            // ذخیره‌ی موقت داده‌های یک ردیف
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

                // **اعتبار سنجی نوع (ساده):**
                if (columns[i].type == "INT") {
                    try {
                        // تلاش برای تبدیل به int
                        (void)stoi(data_input); 
                    } catch (...) {
                        cout << "❌ خطای نوع! لطفاً یک عدد صحیح وارد کنید.\n";
                        i--; // برگشت به ستون قبلی برای ورود مجدد
                        continue; 
                    }
                }
                
                temp_row_data.push_back(data_input);
            }

            if (should_break) {
                cout << "پایان ورود داده.\n";
                break;
            }
            
            // اضافه کردن داده‌های موقت به ستون‌ها
            for (size_t i = 0; i < columns.size(); ++i) {
                columns[i].data.push_back(temp_row_data[i]);
            }

            user_count++;
        }
    }

    // --- ۴. ذخیره‌ی داده‌ها (SAVE) ---
    void save_data(const string& filename) {
        if (columns.empty()) {
            cout << "⚠️ دیتابیس خالی است. چیزی برای ذخیره نیست.\n";
            return;
        }

        ofstream outfile(filename);
        if (!outfile.is_open()) {
            cout << "❌ خطای ذخیره‌سازی! نمی‌توان فایل را باز کرد: " << filename << "\n";
            return;
        }

        // خط ۱: هدرها (نام ستون‌ها)
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
        cout << "✅ داده‌ها با موفقیت در " << filename << " ذخیره شدند.\n";
    }

    // --- ۵. بارگذاری داده‌ها (LOAD) ---
    void load_data(const string& filename) {
        ifstream infile(filename);
        if (!infile.is_open()) {
            cout << "❌ خطای بارگذاری! فایل پیدا نشد یا قابل باز شدن نیست: " << filename << "\n";
            return;
        }

        string header_line;
        if (!getline(infile, header_line)) {
            cout << "❌ خطای بارگذاری! فایل خالی است.\n";
            infile.close();
            return;
        }

        // ۱. ساختار (Schema) را از سربرگ می‌خوانیم
        columns.clear();
        vector<string> headers = from_csv_line(header_line);
        for (const string& h : headers) {
            // فرض می‌کنیم فرمت "نام (نوع)" است
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
            if (row_data.size() != columns.size()) continue; // نادیده گرفتن سطرهای ناقص

            for (size_t i = 0; i < columns.size(); ++i) {
                columns[i].data.push_back(row_data[i]);
            }
        }

        infile.close();
        cout << "✅ داده‌ها و ساختار با موفقیت از " << filename << " بارگذاری شدند.\n";
    }


    // --- ۶. نمایش داده‌ها (VIEW) ---
    void view_data() const {
        if (columns.empty()) {
            cout << "⚠️ دیتابیس خالی است.\n";
            return;
        }
        
        cout << "\n--- نمایش داده‌های دیتابیس (" << columns.front().data.size() << " ردیف) ---\n";

        // چاپ هدر
        for (const auto& col : columns) {
            cout << "| " << col.name << " (" << col.type << ") \t";
        }
        cout << "|\n";
        cout << string(columns.size() * 20, '-') << "\n";

        // چاپ سطرها
        if (columns.empty() || columns.front().data.empty()) return;
        size_t num_rows = columns.front().data.size();

        for (size_t r = 0; r < num_rows; ++r) {
            for (const auto& col : columns) {
                cout << "| " << col.data[r] << " \t\t";
            }
            cout << "|\n";
        }
        cout << string(columns.size() * 20, '-') << "\n";
    }

    // --- ۷. نمایش ساختار (SCHEMA) ---
    void view_schema() const {
        if (columns.empty()) {
            cout << "⚠️ ساختاری تعریف نشده است.\n";
            return;
        }
        cout << "\n--- ساختار دیتابیس ---\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            cout << i + 1 << ". " << columns[i].name << "\t(نوع: " << columns[i].type << ")\n";
        }
    }
};

// === توابع منو و اصلی ===

void display_menu() {
    cout << "\n------------------------------------------------\n";
    cout << "  C: ایجاد ساختار جدید (Create)\n";
    cout << "  CA: ویرایش ساختار (Alter)\n";
    cout << "  run: شروع ورود داده (Add Rows)\n";
    cout << "  VIEW: نمایش داده‌ها\n";
    cout << "  SCHEMA: نمایش ساختار فعلی\n";
    cout << "  SAVE <filename.csv>: ذخیره‌ی داده‌ها\n";
    cout << "  LOAD <filename.csv>: بارگذاری داده‌ها\n";
    cout << "  Q: خروج\n";
    cout << "------------------------------------------------\n";
    cout << "دستور شما: ";
}

int main() {
    // افزایش سرعت ورودی/خروجی
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    SimpleDB db;
    string command_line;

    cout << "🚀 دیتابیس CLI قدرتمند شما خوش آمدید! 🚀\n";

    while (true) {
        display_menu();
        // گرفتن کل خط ورودی برای اجرای دستورات با پارامتر (مثل SAVE mydata.csv)
        getline(cin, command_line);

        if (command_line.empty()) continue;

        // تجزیه خط فرمان
        stringstream ss(command_line);
        string main_command;
        ss >> main_command;

        // تبدیل دستور اصلی به حروف بزرگ برای مقایسه آسان‌تر
        transform(main_command.begin(), main_command.end(), main_command.begin(), ::toupper);

        if (main_command == "C") {
            db.create_schema();
        } else if (main_command == "CA") {
            db.edit_schema();
        } else if (main_command == "RUN") {
            db.run_data_entry();
        } else if (main_command == "VIEW") {
            db.view_data();
        } else if (main_command == "SCHEMA") {
            db.view_schema();
        } else if (main_command == "SAVE") {
            string filename;
            if (ss >> filename) {
                db.save_data(filename);
            } else {
                cout << "⚠️ دستور ناقص! SAVE <نام_فایل.csv>.\n";
            }
        } else if (main_command == "LOAD") {
            string filename;
            if (ss >> filename) {
                db.load_data(filename);
            } else {
                cout << "⚠️ دستور ناقص! LOAD <نام_فایل.csv>.\n";
            }
        } else if (main_command == "Q") {
            cout << "👋 خدانگهدار. موفق باشید در پروژه‌تان!\n";
            break;
        } else {
            cout << "❌ دستور نامعتبر. لطفاً از دستورات منو استفاده کنید.\n";
        }
    }

    return 0;
}
