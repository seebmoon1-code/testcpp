#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <limits>
#include <algorithm>
#include <stdexcept>

// تعریف ساختار داده
using Record = std::vector<std::string>;
using DatabaseTable = std::vector<Record>;

class dbc_c_Database {
private:
    std::string dbName;
    std::string cfgFileName;
    std::string dataFileName;
    std::vector<std::string> fieldNames;
    DatabaseTable data;
    size_t numFields = 0; // تعداد ستون‌ها به صورت پویا

    // --- توابع کمکی ---

    // دریافت ورودی از کاربر (تضمین ورودی تمیز)
    std::string get_user_input(const std::string& prompt) {
        std::string input;
        std::cout << prompt;
        
        // در اینجا از std::cin.ignore استفاده نمی‌کنیم چون با getline مشکل ایجاد می‌کند.
        // تمیزکاری بافر در تابع main توسط std::cin >> choice انجام شده است.

        std::getline(std::cin, input);
        
        // حذف فضای خالی از ابتدا و انتها
        input.erase(0, input.find_first_not_of(" \t\n\r\f\v"));
        input.erase(input.find_last_not_of(" \t\n\r\f\v") + 1);
        
        // نکته مهم: اگر ورودی شامل کاما باشد، برای جلوگیری از مشکلات CSV، آن را حذف یا جایگزین می‌کنیم
        std::replace(input.begin(), input.end(), ',', ' '); 
        
        return input;
    }

    // جدا کردن یک خط CSV به فیلدهای وکتور
    Record parseLine(const std::string& line) const {
        Record record;
        std::stringstream ss(line);
        std::string segment;
        while (std::getline(ss, segment, ',')) {
            record.push_back(segment);
        }
        return record;
    }
    
    // --- مدیریت فایل تنظیمات (Configuration) ---

    void loadConfig() {
        std::ifstream infile(cfgFileName);
        if (!infile.is_open()) return;

        std::string line;
        
        // سطر اول: تعداد فیلدها
        if (std::getline(infile, line)) {
            try {
                numFields = std::stoul(line);
            } catch (...) {
                numFields = 0; 
            }
        }
        
        // سطر دوم: نام فیلدها (CSV-Format)
        if (std::getline(infile, line)) {
            fieldNames = parseLine(line);
            if (fieldNames.size() != numFields) {
                // اگر تعداد ستون‌های ذخیره شده با تعداد نام‌ها نخواند، اصلاح می‌کند
                fieldNames.resize(numFields);
                for(size_t i=0; i<numFields; ++i) {
                     if (fieldNames[i].empty()) fieldNames[i] = "Col" + std::to_string(i + 1);
                }
            }
        }
        infile.close();
    }

    void saveConfig() const {
        std::ofstream outfile(cfgFileName);
        if (!outfile.is_open()) {
            std::cerr << "Error: Could not open config file for saving.\n";
            return;
        }

        // 1. ذخیره تعداد فیلدها
        outfile << numFields << "\n";
        
        // 2. ذخیره نام فیلدها
        for (size_t i = 0; i < fieldNames.size(); ++i) {
            outfile << fieldNames[i] << (i < fieldNames.size() - 1 ? "," : "");
        }
        outfile << "\n";
        outfile.close();
    }

    // --- مدیریت فایل داده (Data) ---

    void loadData() {
        if (numFields == 0) return; 

        std::ifstream infile(dataFileName);
        if (!infile.is_open()) return;

        data.clear();
        std::string line;
        while (std::getline(infile, line)) {
            Record newRecord = parseLine(line);
            newRecord.resize(numFields, ""); 
            data.push_back(newRecord);
        }
        infile.close();
    }

    void saveData() const {
        if (numFields == 0) return;

        std::ofstream outfile(dataFileName);
        if (!outfile.is_open()) {
            std::cerr << "Error: Could not open data file for saving.\n";
            return;
        }

        // ذخیره داده‌ها
        for (const auto& record : data) {
            for (size_t i = 0; i < numFields; ++i) {
                outfile << record[i] << (i < numFields - 1 ? "," : "");
            }
            outfile << "\n";
        }
        outfile.close();
        std::cout << "\nداده‌ها با موفقیت ذخیره شدند.\n";
    }

    // --- تعامل برای تنظیم نام فیلدها ---

    void setFieldNamesInteractively(size_t N) {
        fieldNames.assign(N, "");
        numFields = N;
        std::cout << "\n--- تنظیم نام " << N << " ستون دیتابیس (" << dbName << ") ---\n";
        bool continueSetup = true;

        for (size_t i = 0; i < N; ++i) {
            std::string fieldDefaultName = "Col" + std::to_string(i + 1);
            
            if (!continueSetup) {
                fieldNames[i] = fieldDefaultName;
                continue; 
            }
            
            std::string prompt = "نام ستون " + std::to_string(i + 1) + " (" + fieldDefaultName + ") چی باشه؟ (اینتر برای رد شدن) ";
            // استفاده از تابع کمکی برای دریافت ورودی تمیز
            std::string input = get_user_input(prompt);
            
            if (input.empty()) {
                fieldNames[i] = fieldDefaultName; 
                
                std::cout << "ستون خالی ماند. آیا مایل به تعریف ستون‌های بعدی هستید؟ (Y/n، پیش‌فرض: Y) ";
                std::string cont;
                std::getline(std::cin, cont);
                
                if (cont.empty() || cont[0] == 'n' || cont[0] == 'N') {
                    continueSetup = false;
                }
            } else {
                fieldNames[i] = input;
            }
        }
        saveConfig(); 
        data.clear(); // اگر پیکربندی تغییر کند، داده‌های قبلی باید پاک شوند.
        std::cout << "--- تنظیمات ستون‌ها با موفقیت ذخیره شد. ---\n";
    }

public:
    // سازنده
    dbc_c_Database(const std::string& name) : dbName(name) {
        cfgFileName = "dbc_c_" + name + ".cfg";
        dataFileName = "dbc_c_" + name + ".dat";
        
        loadConfig(); 
        if (numFields > 0) {
            loadData(); 
            std::cout << "دیتابیس '" << name << "' با " << numFields << " ستون و " << data.size() << " رکورد بارگذاری شد.\n";
        } else {
            std::cout << "دیتابیس '" << name << "' ایجاد شد. منتظر پیکربندی ستون‌ها هستید.\n";
        }
    }
    
    // متد کلیدی پیکربندی ستون
    void configureColumns(size_t N) {
        if (N == 0) {
             std::cerr << "تعداد ستون‌ها باید حداقل 1 باشد.\n";
             return;
        }
        
        if (numFields > 0) {
             std::cout << "هشدار: تغییر پیکربندی، داده‌های موجود را پاک می‌کند.\n";
        }
        
        setFieldNamesInteractively(N);
    }
    
    // متد Getter برای دسترسی به نام دیتابیس (حل ارور Private)
    std::string getDBName() const {
        return dbName;
    }

    // متد ورود داده جدید
    void enterNewRecord() {
        if (numFields == 0) {
             std::cerr << "خطا: ابتدا باید تعداد ستون‌ها را تعریف کنید (مانند: " << dbName << "_c_4).\n";
             return;
        }

        Record newRecord(numFields, ""); 
        std::cout << "\n--- وارد کردن داده‌های جدید (رکورد " << data.size() + 1 << ") ---\n";
        
        for (size_t i = 0; i < numFields; ++i) {
            std::string prompt = "ورودی برای [" + fieldNames[i] + "]: ";
            
            // دریافت ورودی از کاربر
            std::string input = get_user_input(prompt);

            if (!input.empty()) {
                newRecord[i] = input;
            }
            
            // شرط شما: اگر آخرین فیلد را پر کرد و اینتر زد، رکورد تکمیل شده و ذخیره می‌شود
            if (i == numFields - 1) { 
                data.push_back(newRecord); 
                saveData(); 
                std::cout << "رکورد " << data.size() << " تکمیل و ذخیره شد. \n";
            }
        }
    }

    // متد نمایش
    void displayDatabase() const {
        if (numFields == 0) {
             std::cerr << "خطا: دیتابیس هنوز پیکربندی نشده است.\n";
             return;
        }
        std::cout << "\n--- نمایش دیتابیس (" << dbName << ") با " << numFields << " ستون ---\n";
        
        // نمایش هدر
        std::cout << "| #\t";
        for (const auto& name : fieldNames) {
            std::cout << "| " << name << "\t";
        }
        std::cout << "|\n";
        
        // نمایش جداکننده
        for (size_t i = 0; i <= numFields; ++i) {
            std::cout << "----------";
        }
        std::cout << "-\n";

        // نمایش داده‌ها
        int rowNum = 1;
        for (const auto& record : data) {
            std::cout << "| " << rowNum++ << "\t";
            for (const auto& value : record) {
                std::cout << "| " << value << "\t";
            }
            std::cout << "|\n";
        }
        std::cout << "--------------------------------\n";
    }
    
    // متد نهایی برای ذخیره در هنگام خروج
    void finalSave() const {
        saveData();
        saveConfig();
    }
};

// --- حلقه اصلی برنامه (Main Loop) ---

// تابع main برای دریافت نام دیتابیس از ترمینال تغییر داده شد.
int main(int argc, char* argv[]) {
    
    // بررسی می‌کند که آیا کاربر نام دیتابیس را وارد کرده است یا خیر
    if (argc < 2) {
        std::cerr << "خطا: لطفا هنگام اجرای برنامه، نام دیتابیس را وارد کنید.\n";
        std::cerr << "مثال: ./nnn moon\n";
        return 1;
    }

    // نام دیتابیس را از آرگومان دوم (argv[1]) می‌خواند
    std::string db_name = argv[1]; 
    
    // دیتابیس را با نامی که از ترمینال گرفته شده است، می‌سازد
    dbc_c_Database myDB(db_name); 
    
    std::string choice;
    bool running = true;

    while (running) {
        std::cout << "\n============================================\n";
        std::cout << "دیتابیس " << db_name << " - عملیات:\n";
        std::cout << "1. پیکربندی ستون‌ها (مثال: " << db_name << "_c_4)\n"; 
        std::cout << "2. وارد کردن رکورد جدید\n";
        std::cout << "3. نمایش کل دیتابیس\n";
        std::cout << "4. خروج\n";
        std::cout << "گزینه خود را وارد کنید (1-4) یا دستور [DB_NAME_c_N]: ";
        
        std::cin >> choice; 
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        
        if (choice == "1") {
            // خط ۲۸۸ اصلاح شده برای استفاده از getDBName()
            std::string countStr = myDB.getDBName() + "_c_"; 
            
            std::cout << "لطفا تعداد ستون‌ها را وارد کنید (فقط عدد N): ";
            std::string N_input;
            std::getline(std::cin, N_input);
            try {
                size_t N = std::stoul(N_input);
                myDB.configureColumns(N);
            } catch (...) {
                std::cerr << "خطا: ورودی نامعتبر برای تعداد ستون.\n";
            }
            
        } else if (choice == "2") {
            myDB.enterNewRecord();
        } else if (choice == "3") {
            myDB.displayDatabase();
        } else if (choice == "4") {
            myDB.finalSave();
            running = false;
        } else {
            // بررسی دستور DB_NAME_c_N (خطوط ۳۰۷ و ۳۰۹ اصلاح شده)
            if (choice.size() > myDB.getDBName().size() + 3 && 
                choice.substr(0, myDB.getDBName().size() + 3) == myDB.getDBName() + "_c_") 
            {
                try {
                    // استخراج N از دستور (خط ۳۰۹ اصلاح شده)
                    size_t N = std::stoul(choice.substr(myDB.getDBName().size() + 3));
                    myDB.configureColumns(N);
                } catch (...) {
                    std::cerr << "خطا: فرمت دستور نامعتبر است. از DB_NAME_c_N استفاده کنید (N عدد باشد).\n";
                }
            } else {
                std::cout << "گزینه یا دستور نامعتبر. دوباره تلاش کنید.\n";
            }
        }
    }

    std::cout << "خداحافظ. دیتابیس بسته شد.\n";
    return 0;
}
